#include "decklink_output.hpp"

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <libyuv.h>

#include "decklink.hpp"

using namespace godot;

DeckLinkOutput::DeckLinkOutput() {
    _ref_count.init();
    _output_mutex = memnew(Mutex);
}

DeckLinkOutput::~DeckLinkOutput() {
    close();
    memdelete(_output_mutex);
    _output_mutex = nullptr;
}

void DeckLinkOutput::_bind_methods() {
    BIND_ENUM_CONSTANT(OUTPUT_FORMAT_AUTO);
    BIND_ENUM_CONSTANT(OUTPUT_FORMAT_BGRA);
    BIND_ENUM_CONSTANT(OUTPUT_FORMAT_YUV);

    ClassDB::bind_method(D_METHOD("open", "device", "display_mode"), &DeckLinkOutput::open, DEFVAL((int64_t)bmdModeHD1080p5994));
    ClassDB::bind_method(D_METHOD("close"), &DeckLinkOutput::close);
    ClassDB::bind_method(D_METHOD("is_open"), &DeckLinkOutput::is_open);
    ClassDB::bind_method(D_METHOD("get_texture"), &DeckLinkOutput::get_texture);
    ClassDB::bind_method(D_METHOD("set_texture", "texture"), &DeckLinkOutput::set_texture);
    ClassDB::bind_method(D_METHOD("is_enabled"), &DeckLinkOutput::is_enabled);
    ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &DeckLinkOutput::set_enabled);
    ClassDB::bind_method(D_METHOD("get_device"), &DeckLinkOutput::get_device);
    ClassDB::bind_method(D_METHOD("set_device", "device"), &DeckLinkOutput::set_device);
    ClassDB::bind_method(D_METHOD("get_display_mode"), &DeckLinkOutput::get_display_mode);
    ClassDB::bind_method(D_METHOD("set_display_mode", "display_mode"), &DeckLinkOutput::set_display_mode);
    ClassDB::bind_method(D_METHOD("get_connection"), &DeckLinkOutput::get_connection);
    ClassDB::bind_method(D_METHOD("set_connection", "connection"), &DeckLinkOutput::set_connection);
    ClassDB::bind_method(D_METHOD("get_output_format"), &DeckLinkOutput::get_output_format);
    ClassDB::bind_method(D_METHOD("set_output_format", "output_format"), &DeckLinkOutput::set_output_format);
    ClassDB::bind_method(D_METHOD("get_width"), &DeckLinkOutput::get_width);
    ClassDB::bind_method(D_METHOD("get_height"), &DeckLinkOutput::get_height);

    ClassDB::add_property("DeckLinkOutput",
            { Variant::BOOL, "enabled" },
            "set_enabled", "is_enabled");
    ClassDB::add_property("DeckLinkOutput",
            { Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D" },
            "set_texture", "get_texture");
    ClassDB::add_property("DeckLinkOutput",
            { Variant::INT, "device", PROPERTY_HINT_ENUM },
            "set_device", "get_device");
    ClassDB::add_property("DeckLinkOutput",
            { Variant::INT, "display_mode", PROPERTY_HINT_ENUM },
            "set_display_mode", "get_display_mode");
    ClassDB::add_property("DeckLinkOutput",
            { Variant::INT, "connection", PROPERTY_HINT_ENUM, "Unspecified:0,SDI:1,HDMI:2,Optical SDI:4,Component:8,Composite:16,S-Video:32" },
            "set_connection", "get_connection");
    ClassDB::add_property("DeckLinkOutput",
            { Variant::INT, "output_format", PROPERTY_HINT_ENUM, "Auto,BGRA,YUV" },
            "set_output_format", "get_output_format");
}

HRESULT DeckLinkOutput::QueryInterface(REFIID p_iid, LPVOID *r_ppv) {
    if (!r_ppv) {
        return E_INVALIDARG;
    }
    if (decklink::iid_equal(p_iid, IID_IUnknown) || decklink::iid_equal(p_iid, IID_IDeckLinkVideoOutputCallback)) {
        *r_ppv = static_cast<IDeckLinkVideoOutputCallback *>(this);
        AddRef();
        return S_OK;
    }
    *r_ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG DeckLinkOutput::AddRef() {
    return _ref_count.refval();
}

ULONG DeckLinkOutput::Release() {
    return _ref_count.unrefval();
}

void DeckLinkOutput::_ready() {
    if (_enabled) {
        set_enabled(true);
    }
}

void DeckLinkOutput::_exit_tree() {
    _disconnect_frame_post_draw();
    close();
}

void DeckLinkOutput::_validate_property(PropertyInfo &p_property) const {
    const String name = p_property.name;
    if (name == "device") {
        p_property.hint = PROPERTY_HINT_ENUM;
        p_property.hint_string = _get_device_hint_string();
    } else if (name == "display_mode") {
        p_property.hint = PROPERTY_HINT_ENUM;
        p_property.hint_string = _get_display_mode_hint_string();
    }
}

bool DeckLinkOutput::open(int p_device, int64_t p_display_mode) {
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

    if (!_decklink_device->query_output(&_output)) {
        close();
        return false;
    }

    bool supported_bgra = false;
    bool supported_yuv = false;
    BMDDisplayMode actual_mode = bmdModeUnknown;
    if (_output_format == OUTPUT_FORMAT_AUTO || _output_format == OUTPUT_FORMAT_BGRA) {
        _output->DoesSupportVideoMode(_connection, _display_mode, bmdFormat8BitBGRA, bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault, &actual_mode, &supported_bgra);
    }
    if (_output_format == OUTPUT_FORMAT_AUTO || _output_format == OUTPUT_FORMAT_YUV || !supported_bgra) {
        _output->DoesSupportVideoMode(_connection, _display_mode, bmdFormat8BitYUV, bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault, &actual_mode, &supported_yuv);
    }

    if (!supported_bgra && !supported_yuv) {
        UtilityFunctions::printerr("[DeckLinkOutput] Display mode is not supported for selected output format.");
        close();
        return false;
    }

    _pixel_format = supported_bgra ? bmdFormat8BitBGRA : bmdFormat8BitYUV;
    if (actual_mode != bmdModeUnknown) {
        _display_mode = actual_mode;
    }

    IDeckLinkDisplayMode *mode = nullptr;
    if (_output->GetDisplayMode(_display_mode, &mode) != S_OK || !mode) {
        close();
        return false;
    }
    _width = mode->GetWidth();
    _height = mode->GetHeight();
    mode->GetFrameRate(&_frame_duration, &_time_scale);
    mode->Release();

    HRESULT result = _output->EnableVideoOutput(_display_mode, bmdVideoOutputFlagDefault);
    if (result != S_OK) {
        UtilityFunctions::printerr("[DeckLinkOutput] EnableVideoOutput failed: ",
                decklink::hresult_name(result),
                " (", (int64_t)result, ") mode=", (int64_t)_display_mode,
                " pixel_format=", (int64_t)_pixel_format);
        close();
        return false;
    }

    _open = true;
    return true;
}

void DeckLinkOutput::close() {
    _stop_output_thread();

    if (_output) {
        _output->DisableVideoOutput();
    }

    decklink::safe_release(_output);
    _decklink_device.unref();
    _open = false;
    _width = 0;
    _height = 0;
    {
        MutexLock lock(*_output_mutex);
        _latest_rgba.clear();
        _has_output_frame = false;
    }
}

bool DeckLinkOutput::is_open() const {
    return _open;
}

int DeckLinkOutput::get_width() const {
    return _width;
}

int DeckLinkOutput::get_height() const {
    return _height;
}

bool DeckLinkOutput::_output_frame(const PackedByteArray &p_rgba) {
    if (!_open || !_output || p_rgba.is_empty()) {
        return false;
    }
    if (p_rgba.size() < _width * _height * 4) {
        return false;
    }

    const uint8_t *src = p_rgba.ptr();

    const int row_bytes = _pixel_format == bmdFormat8BitBGRA ? _width * 4 : _width * 2;

    IDeckLinkMutableVideoFrame *frame = nullptr;
    HRESULT result = _output->CreateVideoFrame(_width, _height, row_bytes, _pixel_format, bmdFrameFlagDefault, &frame);
    if (result != S_OK || !frame) {
        UtilityFunctions::printerr("[DeckLinkOutput] CreateVideoFrame failed: ", (int64_t)result);
        return false;
    }

    IDeckLinkVideoBuffer *buffer = nullptr;
    if (frame->QueryInterface(IID_IDeckLinkVideoBuffer, (void **)&buffer) != S_OK || !buffer) {
        frame->Release();
        return false;
    }

    void *frame_bytes = nullptr;
    bool ok = false;
    bool access_started = false;
    if (buffer->StartAccess(bmdBufferAccessWrite) == S_OK) {
        access_started = true;
    }
    if (access_started && buffer->GetBytes(&frame_bytes) == S_OK && frame_bytes) {
        uint8_t *dst = static_cast<uint8_t *>(frame_bytes);
        // Godot RGBA8 is ABGR in libyuv terms. DeckLink BGRA is libyuv ARGB.
        if (_pixel_format == bmdFormat8BitBGRA) {
            ok = libyuv::ABGRToARGB(src, _width * 4, dst, row_bytes, _width, _height) == 0;
        } else {
            PackedByteArray argb;
            argb.resize(_width * _height * 4);
            uint8_t *argb_bytes = argb.ptrw();
            ok = libyuv::ABGRToARGB(src, _width * 4, argb_bytes, _width * 4, _width, _height) == 0 &&
                    libyuv::ARGBToUYVY(argb_bytes, _width * 4, dst, row_bytes, _width, _height) == 0;
        }
        if (!ok) {
            UtilityFunctions::printerr("[DeckLinkOutput] libyuv conversion failed: pixel_format=", (int64_t)_pixel_format);
        } else {
            result = _output->DisplayVideoFrameSync(frame);
            ok = result == S_OK;
            if (!ok) {
                UtilityFunctions::printerr("[DeckLinkOutput] DisplayVideoFrameSync failed: ",
                        decklink::hresult_name(result), " (", (int64_t)result, ")");
            }
        }
    }
    if (access_started) {
        buffer->EndAccess(bmdBufferAccessWrite);
    }
    buffer->Release();
    frame->Release();
    return ok;
}

Ref<Texture2D> DeckLinkOutput::get_texture() const {
    return _texture;
}

void DeckLinkOutput::set_texture(Ref<Texture2D> p_texture) {
    _texture = p_texture;
}

bool DeckLinkOutput::is_enabled() const {
    return _enabled;
}

void DeckLinkOutput::set_enabled(bool p_enabled) {
    _enabled = p_enabled;
    if (_enabled) {
        if (!_open && !open(_device, _display_mode)) {
            _enabled = false;
            return;
        }
        _start_output_thread();
        _connect_frame_post_draw();
    } else {
        _disconnect_frame_post_draw();
        close();
    }
}

int DeckLinkOutput::get_device() const {
    return _device;
}

void DeckLinkOutput::set_device(int p_device) {
    if (_device == p_device) {
        return;
    }
    _device = p_device;
    notify_property_list_changed();
    _restart_if_enabled();
}

int64_t DeckLinkOutput::get_display_mode() const {
    return (int64_t)_display_mode;
}

void DeckLinkOutput::set_display_mode(int64_t p_display_mode) {
    if (_display_mode == (BMDDisplayMode)p_display_mode) {
        return;
    }
    _display_mode = (BMDDisplayMode)p_display_mode;
    _restart_if_enabled();
}

int64_t DeckLinkOutput::get_connection() const {
    return (int64_t)_connection;
}

void DeckLinkOutput::set_connection(int64_t p_connection) {
    if (_connection == (BMDVideoConnection)p_connection) {
        return;
    }
    _connection = (BMDVideoConnection)p_connection;
    _restart_if_enabled();
}

DeckLinkOutput::OutputFormat DeckLinkOutput::get_output_format() const {
    return _output_format;
}

void DeckLinkOutput::set_output_format(OutputFormat p_output_format) {
    if (_output_format == p_output_format) {
        return;
    }
    _output_format = p_output_format;
    _restart_if_enabled();
}

void DeckLinkOutput::_on_frame_post_draw() {
    if (_enabled) {
        _capture_texture();
    }
}

void DeckLinkOutput::_connect_frame_post_draw() {
    RenderingServer *rendering_server = RenderingServer::get_singleton();
    if (!rendering_server || _frame_post_draw_connected) {
        return;
    }

    Callable callable = callable_mp(this, &DeckLinkOutput::_on_frame_post_draw);
    if (!rendering_server->is_connected("frame_post_draw", callable)) {
        rendering_server->connect("frame_post_draw", callable);
    }
    _frame_post_draw_connected = true;
}

void DeckLinkOutput::_disconnect_frame_post_draw() {
    RenderingServer *rendering_server = RenderingServer::get_singleton();
    if (!rendering_server || !_frame_post_draw_connected) {
        return;
    }

    Callable callable = callable_mp(this, &DeckLinkOutput::_on_frame_post_draw);
    if (rendering_server->is_connected("frame_post_draw", callable)) {
        rendering_server->disconnect("frame_post_draw", callable);
    }
    _frame_post_draw_connected = false;
}

void DeckLinkOutput::_restart_if_enabled() {
    if (!_enabled) {
        return;
    }

    close();
    if (!open(_device, _display_mode)) {
        _disconnect_frame_post_draw();
        _enabled = false;
        return;
    }
    _start_output_thread();
}

void DeckLinkOutput::_capture_texture() {
    if (!_open || _texture.is_null()) {
        return;
    }

    Ref<Image> texture_image = _texture->get_image();
    if (texture_image.is_null()) {
        return;
    }

    Ref<Image> image = texture_image->duplicate();
    if (image->get_width() != _width || image->get_height() != _height) {
        image->resize(_width, _height);
    }
    if (image->get_format() != Image::FORMAT_RGBA8) {
        image->convert(Image::FORMAT_RGBA8);
    }

    PackedByteArray rgba = image->get_data();
    {
        MutexLock lock(*_output_mutex);
        _latest_rgba = rgba;
        _has_output_frame = !_latest_rgba.is_empty();
    }
}

void DeckLinkOutput::_output_thread_loop() {
    const int64_t fallback_frame_usec = 16667;
    int64_t frame_usec = fallback_frame_usec;
    if (_time_scale != 0) {
        frame_usec = (int64_t)(_frame_duration * 1000000 / _time_scale);
        if (frame_usec <= 0) {
            frame_usec = fallback_frame_usec;
        }
    }

    OS *os = OS::get_singleton();
    while (!_is_output_thread_stop_requested()) {
        PackedByteArray rgba;
        {
            MutexLock lock(*_output_mutex);
            if (_has_output_frame) {
                rgba = _latest_rgba;
            }
        }

        if (!rgba.is_empty()) {
            _output_frame(rgba);
        }

        if (os) {
            os->delay_usec((int32_t)frame_usec);
        } else {
            break;
        }
    }
}

void DeckLinkOutput::_start_output_thread() {
    if (_output_thread.is_valid() && _output_thread->is_started()) {
        return;
    }

    {
        MutexLock lock(*_output_mutex);
        _output_thread_stop_requested = false;
    }

    _output_thread.instantiate();
    const Error error = _output_thread->start(callable_mp(this, &DeckLinkOutput::_output_thread_loop), Thread::PRIORITY_HIGH);
    if (error != OK) {
        UtilityFunctions::printerr("[DeckLinkOutput] Could not start output thread: ", (int64_t)error);
        _output_thread.unref();
    }
}

void DeckLinkOutput::_stop_output_thread() {
    if (_output_thread.is_null()) {
        return;
    }

    {
        MutexLock lock(*_output_mutex);
        _output_thread_stop_requested = true;
    }

    if (_output_thread->is_started()) {
        _output_thread->wait_to_finish();
    }
    _output_thread.unref();
}

bool DeckLinkOutput::_is_output_thread_stop_requested() const {
    MutexLock lock(*_output_mutex);
    return _output_thread_stop_requested;
}

String DeckLinkOutput::_get_device_hint_string() const {
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

String DeckLinkOutput::_get_display_mode_hint_string() const {
    DeckLink *decklink = DeckLink::get_singleton();
    if (!decklink) {
        return "1080p59.94:" + String::num_int64((int64_t)bmdModeHD1080p5994);
    }

    Ref<DeckLinkDevice> device = decklink->get_device(_device);
    if (device.is_null()) {
        return "1080p59.94:" + String::num_int64((int64_t)bmdModeHD1080p5994);
    }

    const Array modes = device->get_output_display_modes();
    String hint;
    for (int i = 0; i < modes.size(); ++i) {
        const Dictionary mode = modes[i];
        String name = mode.get("name", String());
        if (name.is_empty()) {
            name = String::num_int64((int64_t)mode.get("width", 0)) + "x" + String::num_int64((int64_t)mode.get("height", 0));
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

HRESULT DeckLinkOutput::ScheduledFrameCompleted(IDeckLinkVideoFrame *p_completed_frame, BMDOutputFrameCompletionResult p_result) {
    return S_OK;
}

HRESULT DeckLinkOutput::ScheduledPlaybackHasStopped() {
    return S_OK;
}
