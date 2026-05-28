#include "decklink_input.hpp"

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <libyuv.h>

#include "decklink.hpp"

using namespace godot;

static bool copy_frame_to_rgba(IDeckLinkVideoFrame *p_frame,
                               PackedByteArray &r_rgba) {
  if (!p_frame) {
    return false;
  }

  IDeckLinkVideoBuffer *buffer = nullptr;
  if (p_frame->QueryInterface(IID_IDeckLinkVideoBuffer, (void **)&buffer) !=
          S_OK ||
      !buffer) {
    return false;
  }

  void *bytes = nullptr;
  const int width = p_frame->GetWidth();
  const int height = p_frame->GetHeight();
  const int row_bytes = p_frame->GetRowBytes();
  bool ok = false;
  bool access_started = false;

  if (buffer->StartAccess(bmdBufferAccessRead) == S_OK) {
    access_started = true;
  }
  if (access_started && buffer->GetBytes(&bytes) == S_OK && bytes) {
    const uint8_t *src = static_cast<const uint8_t *>(bytes);
    r_rgba.resize(width * height * 4);
    uint8_t *dst = r_rgba.ptrw();

    // libyuv ARGB is BGRA in memory. Godot RGBA8 is ABGR in libyuv terms.
    if (p_frame->GetPixelFormat() == bmdFormat8BitBGRA) {
      ok = libyuv::ARGBToABGR(src, row_bytes, dst, width * 4, width, height) ==
           0;
    } else if (p_frame->GetPixelFormat() == bmdFormat8BitYUV) {
      PackedByteArray argb;
      argb.resize(width * height * 4);
      uint8_t *argb_bytes = argb.ptrw();
      ok = libyuv::UYVYToARGB(src, row_bytes, argb_bytes, width * 4, width,
                              height) == 0 &&
           libyuv::ARGBToABGR(argb_bytes, width * 4, dst, width * 4, width,
                              height) == 0;
    }
  }

  if (access_started) {
    buffer->EndAccess(bmdBufferAccessRead);
  }
  buffer->Release();
  return ok;
}

DeckLinkInput::DeckLinkInput() {
  _ref_count.init();
  _frame_mutex = memnew(Mutex);
}

DeckLinkInput::~DeckLinkInput() {
  close();
  memdelete(_frame_mutex);
  _frame_mutex = nullptr;
}

void DeckLinkInput::_bind_methods() {
  ClassDB::bind_method(D_METHOD("open", "device", "display_mode"),
                       &DeckLinkInput::open,
                       DEFVAL((int64_t)bmdModeHD1080p5994));
  ClassDB::bind_method(D_METHOD("close"), &DeckLinkInput::close);
  ClassDB::bind_method(D_METHOD("is_open"), &DeckLinkInput::is_open);
  ClassDB::bind_method(D_METHOD("is_enabled"), &DeckLinkInput::is_enabled);
  ClassDB::bind_method(D_METHOD("set_enabled", "enabled"),
                       &DeckLinkInput::set_enabled);
  ClassDB::bind_method(D_METHOD("get_device"), &DeckLinkInput::get_device);
  ClassDB::bind_method(D_METHOD("set_device", "device"),
                       &DeckLinkInput::set_device);
  ClassDB::bind_method(D_METHOD("get_display_mode"),
                       &DeckLinkInput::get_display_mode);
  ClassDB::bind_method(D_METHOD("set_display_mode", "display_mode"),
                       &DeckLinkInput::set_display_mode);
  ClassDB::bind_method(D_METHOD("get_texture"), &DeckLinkInput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "texture"),
                       &DeckLinkInput::set_texture);
  ClassDB::bind_method(D_METHOD("has_frame"), &DeckLinkInput::has_frame);
  ClassDB::bind_method(D_METHOD("get_width"), &DeckLinkInput::get_width);
  ClassDB::bind_method(D_METHOD("get_height"), &DeckLinkInput::get_height);

  ClassDB::add_property("DeckLinkInput", {Variant::BOOL, "enabled"},
                        "set_enabled", "is_enabled");
  ClassDB::add_property("DeckLinkInput",
                        {Variant::INT, "device", PROPERTY_HINT_ENUM},
                        "set_device", "get_device");
  ClassDB::add_property("DeckLinkInput",
                        {Variant::INT, "display_mode", PROPERTY_HINT_ENUM},
                        "set_display_mode", "get_display_mode");
  ClassDB::add_property(
      "DeckLinkInput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "ImageTexture"},
      "set_texture", "get_texture");
}

HRESULT DeckLinkInput::QueryInterface(REFIID p_iid, LPVOID *r_ppv) {
  if (!r_ppv) {
    return E_INVALIDARG;
  }
  if (decklink::iid_equal(p_iid, decklink::iid_unknown()) ||
      decklink::iid_equal(p_iid, IID_IDeckLinkInputCallback)) {
    *r_ppv = static_cast<IDeckLinkInputCallback *>(this);
    AddRef();
    return S_OK;
  }
  *r_ppv = nullptr;
  return E_NOINTERFACE;
}

ULONG DeckLinkInput::AddRef() { return _ref_count.refval(); }

ULONG DeckLinkInput::Release() { return _ref_count.unrefval(); }

void DeckLinkInput::_ready() {
  set_process(true);
  if (_enabled) {
    set_enabled(true);
  }
}

void DeckLinkInput::_process(double p_delta) {
  (void)p_delta;
  _update_texture();
}

void DeckLinkInput::_exit_tree() { close(); }

void DeckLinkInput::_validate_property(PropertyInfo &p_property) const {
  const String name = p_property.name;
  if (name == "device") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_device_hint_string();
  } else if (name == "display_mode") {
    p_property.hint = PROPERTY_HINT_ENUM;
    p_property.hint_string = _get_display_mode_hint_string();
  }
}

bool DeckLinkInput::open(int p_device, int64_t p_display_mode) {
  close();
  _device = p_device;
  _display_mode = (BMDDisplayMode)p_display_mode;

  DeckLink *decklink = DeckLink::get_singleton();
  if (!decklink) {
    return false;
  }

  _decklink_device = decklink->get_device(p_device);
  if (_decklink_device.is_null()) {
    return false;
  }

  if (!_decklink_device->query_input(&_decklink_input)) {
    close();
    return false;
  }

  _input_flags = bmdVideoInputFlagDefault;

  IDeckLinkProfileAttributes *attributes = nullptr;
  if (_decklink_device->query_attributes(&attributes)) {
    decklink::Bool supports_format_detection = false;
    if (attributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection,
                            &supports_format_detection) == S_OK &&
        supports_format_detection) {
      _input_flags = bmdVideoInputEnableFormatDetection;
    }
    attributes->Release();
  }

  decklink::Bool supported_bgra = false;
  decklink::Bool supported_yuv = false;
  BMDDisplayMode actual_mode = bmdModeUnknown;
  _decklink_input->DoesSupportVideoMode(
      bmdVideoConnectionUnspecified, _display_mode, bmdFormat8BitBGRA,
      bmdNoVideoInputConversion, bmdSupportedVideoModeDefault, &actual_mode,
      &supported_bgra);
  if (!supported_bgra) {
    _decklink_input->DoesSupportVideoMode(
        bmdVideoConnectionUnspecified, _display_mode, bmdFormat8BitYUV,
        bmdNoVideoInputConversion, bmdSupportedVideoModeDefault, &actual_mode,
        &supported_yuv);
  }

  if (!supported_bgra && !supported_yuv) {
    UtilityFunctions::printerr(
        "[DeckLinkInput] Display mode is not supported for BGRA or YUV input.");
    close();
    return false;
  }

  _pixel_format = supported_bgra ? bmdFormat8BitBGRA : bmdFormat8BitYUV;
  if (actual_mode != bmdModeUnknown) {
    _display_mode = actual_mode;
  }

  IDeckLinkDisplayMode *mode = nullptr;
  if (_decklink_input->GetDisplayMode(_display_mode, &mode) != S_OK || !mode) {
    close();
    return false;
  }
  _width = mode->GetWidth();
  _height = mode->GetHeight();
  mode->Release();

  {
    MutexLock lock(*_frame_mutex);
    _latest_rgba.resize(_width * _height * 4);
    decklink::safe_release(_pending_frame);
    _has_frame = false;
    _texture_dirty = false;
  }

  HRESULT result = _decklink_input->SetCallback(this);
  if (result != S_OK) {
    UtilityFunctions::printerr("[DeckLinkInput] SetCallback failed: ",
                               (int64_t)result);
    close();
    return false;
  }

  result = _decklink_input->EnableVideoInput(_display_mode, _pixel_format,
                                             _input_flags);
  if (result != S_OK) {
    UtilityFunctions::printerr(
        "[DeckLinkInput] EnableVideoInput failed: ", (int64_t)result,
        " mode=", (int64_t)_display_mode,
        " pixel_format=", (int64_t)_pixel_format,
        " flags=", (int64_t)_input_flags);
    close();
    return false;
  }

  _start_input_thread();

  result = _decklink_input->StartStreams();
  if (result != S_OK) {
    UtilityFunctions::printerr("[DeckLinkInput] StartStreams failed: ",
                               (int64_t)result);
    close();
    return false;
  }

  _open = true;
  _enabled = true;
  return true;
}

void DeckLinkInput::close() {
  if (_decklink_input) {
    _decklink_input->StopStreams();
    _decklink_input->DisableVideoInput();
    _decklink_input->SetCallback(nullptr);
  }

  _stop_input_thread();

  decklink::safe_release(_decklink_input);
  _decklink_device.unref();

  MutexLock lock(*_frame_mutex);
  decklink::safe_release(_pending_frame);
  _latest_rgba.clear();
  _has_frame = false;
  _texture_dirty = false;
  _open = false;
  _enabled = false;
  _width = 0;
  _height = 0;
}

bool DeckLinkInput::is_open() const { return _open; }

bool DeckLinkInput::is_enabled() const { return _enabled; }

void DeckLinkInput::set_enabled(bool p_enabled) {
  if (_enabled == p_enabled && _open == p_enabled) {
    return;
  }

  if (p_enabled) {
    if (!open(_device, _display_mode)) {
      _enabled = false;
    }
  } else {
    close();
  }
}

int DeckLinkInput::get_device() const { return _device; }

void DeckLinkInput::set_device(int p_device) {
  if (_device == p_device) {
    return;
  }
  _device = p_device;
  notify_property_list_changed();
  _restart_if_enabled();
}

int64_t DeckLinkInput::get_display_mode() const {
  return (int64_t)_display_mode;
}

void DeckLinkInput::set_display_mode(int64_t p_display_mode) {
  if (_display_mode == (BMDDisplayMode)p_display_mode) {
    return;
  }
  _display_mode = (BMDDisplayMode)p_display_mode;
  _restart_if_enabled();
}

Ref<ImageTexture> DeckLinkInput::get_texture() const { return _texture; }

void DeckLinkInput::set_texture(Ref<ImageTexture> p_texture) {
  _texture = p_texture;
  MutexLock lock(*_frame_mutex);
  if (!_texture.is_null() && _has_frame) {
    _texture_dirty = true;
  }
}

bool DeckLinkInput::has_frame() const {
  MutexLock lock(*_frame_mutex);
  return _has_frame;
}

int DeckLinkInput::get_width() const { return _width; }

int DeckLinkInput::get_height() const { return _height; }

HRESULT DeckLinkInput::VideoInputFormatChanged(
    BMDVideoInputFormatChangedEvents p_events,
    IDeckLinkDisplayMode *p_new_display_mode,
    BMDDetectedVideoInputFormatFlags p_flags) {
  if (!_decklink_input || !p_new_display_mode) {
    return S_OK;
  }

  if (p_events &
      (bmdVideoInputDisplayModeChanged | bmdVideoInputColorspaceChanged)) {
    _decklink_input->StopStreams();
    _decklink_input->FlushStreams();
    _display_mode = p_new_display_mode->GetDisplayMode();
    _width = p_new_display_mode->GetWidth();
    _height = p_new_display_mode->GetHeight();
    {
      MutexLock lock(*_frame_mutex);
      _latest_rgba.resize(_width * _height * 4);
      decklink::safe_release(_pending_frame);
      _has_frame = false;
      _texture_dirty = false;
    }
    _decklink_input->EnableVideoInput(_display_mode, _pixel_format,
                                      _input_flags);
    return _decklink_input->StartStreams();
  }

  return S_OK;
}

void DeckLinkInput::_restart_if_enabled() {
  if (!_enabled) {
    return;
  }

  close();
  if (!open(_device, _display_mode)) {
    _enabled = false;
  }
}

String DeckLinkInput::_get_device_hint_string() const {
  DeckLink *decklink = DeckLink::get_singleton();
  if (!decklink) {
    return "Device 0:0";
  }

  const Array devices = decklink->get_devices();
  String hint;
  for (int i = 0; i < devices.size(); ++i) {
    const Dictionary device = devices[i];
    String name = device.get("display_name", String());
    if (name.is_empty()) {
      name = device.get("model_name", String());
    }
    if (name.is_empty()) {
      name = "Device " + String::num_int64(i);
    }
    name = name.replace(",", " ").replace(":", " ");

    if (!hint.is_empty()) {
      hint += ",";
    }
    hint += name + ":" + String::num_int64(i);
  }

  if (hint.is_empty()) {
    hint = "Device 0:0";
  }
  return hint;
}

String DeckLinkInput::_get_display_mode_hint_string() const {
  DeckLink *decklink = DeckLink::get_singleton();
  if (!decklink) {
    return "1080p59.94:" + String::num_int64((int64_t)bmdModeHD1080p5994);
  }

  Ref<DeckLinkDevice> device = decklink->get_device(_device);
  if (device.is_null()) {
    return "1080p59.94:" + String::num_int64((int64_t)bmdModeHD1080p5994);
  }

  const Array modes = device->get_input_display_modes();
  String hint;
  for (int i = 0; i < modes.size(); ++i) {
    const Dictionary mode = modes[i];
    String name = mode.get("name", String());
    if (name.is_empty()) {
      name = String::num_int64((int64_t)mode.get("width", 0)) + "x" +
             String::num_int64((int64_t)mode.get("height", 0));
    }
    name = name.replace(",", " ").replace(":", " ");

    const int64_t id = mode.get("id", (int64_t)bmdModeHD1080p5994);
    if (!hint.is_empty()) {
      hint += ",";
    }
    hint += name + ":" + String::num_int64(id);
  }

  if (hint.is_empty()) {
    hint = "1080p59.94:" + String::num_int64((int64_t)bmdModeHD1080p5994);
  }
  return hint;
}

void DeckLinkInput::_input_thread_loop() {
  OS *os = OS::get_singleton();
  while (!_is_input_thread_stop_requested()) {
    IDeckLinkVideoInputFrame *frame = nullptr;
    {
      MutexLock lock(*_frame_mutex);
      frame = _pending_frame;
      _pending_frame = nullptr;
    }

    if (frame) {
      PackedByteArray rgba;
      if (copy_frame_to_rgba(frame, rgba)) {
        MutexLock lock(*_frame_mutex);
        _width = frame->GetWidth();
        _height = frame->GetHeight();
        _latest_rgba = rgba;
        _has_frame = true;
        _texture_dirty = true;
      } else {
        UtilityFunctions::printerr(
            "[DeckLinkInput] Could not convert frame to RGBA: pixel_format=",
            (int64_t)frame->GetPixelFormat());
      }
      frame->Release();
    } else if (os) {
      os->delay_usec(1000);
    } else {
      break;
    }
  }
}

void DeckLinkInput::_start_input_thread() {
  if (_input_thread.is_valid() && _input_thread->is_started()) {
    return;
  }

  {
    MutexLock lock(*_frame_mutex);
    _input_thread_stop_requested = false;
  }

  _input_thread.instantiate();
  const Error error = _input_thread->start(
      callable_mp(this, &DeckLinkInput::_input_thread_loop),
      Thread::PRIORITY_HIGH);
  if (error != OK) {
    UtilityFunctions::printerr("[DeckLinkInput] Could not start input thread: ",
                               (int64_t)error);
    _input_thread.unref();
  }
}

void DeckLinkInput::_stop_input_thread() {
  if (_input_thread.is_null()) {
    _clear_pending_frame();
    return;
  }

  {
    MutexLock lock(*_frame_mutex);
    _input_thread_stop_requested = true;
  }

  if (_input_thread->is_started()) {
    _input_thread->wait_to_finish();
  }
  _input_thread.unref();
  _clear_pending_frame();
}

bool DeckLinkInput::_is_input_thread_stop_requested() const {
  MutexLock lock(*_frame_mutex);
  return _input_thread_stop_requested;
}

void DeckLinkInput::_clear_pending_frame() {
  MutexLock lock(*_frame_mutex);
  decklink::safe_release(_pending_frame);
}

void DeckLinkInput::_update_texture() {
  if (_texture.is_null()) {
    return;
  }

  PackedByteArray rgba;
  int width = 0;
  int height = 0;
  {
    MutexLock lock(*_frame_mutex);
    if (!_texture_dirty || !_has_frame || _latest_rgba.is_empty()) {
      return;
    }
    rgba = _latest_rgba;
    width = _width;
    height = _height;
    _texture_dirty = false;
  }

  if (width <= 0 || height <= 0) {
    return;
  }

  Ref<Image> image =
      Image::create_from_data(width, height, false, Image::FORMAT_RGBA8, rgba);
  if (image.is_null()) {
    return;
  }

  if (_texture->get_width() != width || _texture->get_height() != height ||
      _texture->get_format() != Image::FORMAT_RGBA8) {
    _texture->set_image(image);
  } else {
    _texture->update(image);
  }
}

HRESULT DeckLinkInput::VideoInputFrameArrived(
    IDeckLinkVideoInputFrame *p_video_frame,
    IDeckLinkAudioInputPacket *p_audio_packet) {
  if (!p_video_frame ||
      (p_video_frame->GetFlags() & bmdFrameHasNoInputSource)) {
    return S_OK;
  }

  {
    MutexLock lock(*_frame_mutex);
    decklink::safe_release(_pending_frame);
    _pending_frame = p_video_frame;
    _pending_frame->AddRef();
  }
  return S_OK;
}
