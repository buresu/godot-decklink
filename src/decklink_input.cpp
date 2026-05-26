#include "decklink_input.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <libyuv.h>

#include "decklink.hpp"

using namespace godot;

static bool copy_frame_to_rgba(IDeckLinkVideoFrame *p_frame, PackedByteArray &r_rgba) {
    if (!p_frame) {
        return false;
    }

    IDeckLinkVideoBuffer *buffer = nullptr;
    if (p_frame->QueryInterface(IID_IDeckLinkVideoBuffer, (void **)&buffer) != S_OK || !buffer) {
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
            ok = libyuv::ARGBToABGR(src, row_bytes, dst, width * 4, width, height) == 0;
        } else if (p_frame->GetPixelFormat() == bmdFormat8BitYUV) {
            PackedByteArray argb;
            argb.resize(width * height * 4);
            uint8_t *argb_bytes = argb.ptrw();
            ok = libyuv::UYVYToARGB(src, row_bytes, argb_bytes, width * 4, width, height) == 0 &&
                    libyuv::ARGBToABGR(argb_bytes, width * 4, dst, width * 4, width, height) == 0;
        }
    }

    if (access_started) {
        buffer->EndAccess(bmdBufferAccessRead);
    }
    buffer->Release();
    return ok;
}

DeckLinkInput::DeckLinkInput() = default;

DeckLinkInput::~DeckLinkInput() {
    close();
}

void DeckLinkInput::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open", "device_index", "display_mode"), &DeckLinkInput::open, DEFVAL((int64_t)bmdModeHD1080p5994));
    ClassDB::bind_method(D_METHOD("close"), &DeckLinkInput::close);
    ClassDB::bind_method(D_METHOD("is_open"), &DeckLinkInput::is_open);
    ClassDB::bind_method(D_METHOD("has_frame"), &DeckLinkInput::has_frame);
    ClassDB::bind_method(D_METHOD("get_image"), &DeckLinkInput::get_image);
    ClassDB::bind_method(D_METHOD("get_width"), &DeckLinkInput::get_width);
    ClassDB::bind_method(D_METHOD("get_height"), &DeckLinkInput::get_height);
}

HRESULT DeckLinkInput::QueryInterface(REFIID p_iid, LPVOID *r_ppv) {
    if (!r_ppv) {
        return E_INVALIDARG;
    }
    if (decklink::iid_equal(p_iid, IID_IUnknown) || decklink::iid_equal(p_iid, IID_IDeckLinkInputCallback)) {
        *r_ppv = static_cast<IDeckLinkInputCallback *>(this);
        AddRef();
        return S_OK;
    }
    *r_ppv = nullptr;
    return E_NOINTERFACE;
}

ULONG DeckLinkInput::AddRef() {
    return ++_ref_count;
}

ULONG DeckLinkInput::Release() {
    const ULONG count = --_ref_count;
    return count;
}

bool DeckLinkInput::open(int p_device_index, int64_t p_display_mode) {
    close();

    DeckLink *decklink = DeckLink::get_singleton();
    if (!decklink) {
        return false;
    }

    _device = decklink->get_device(p_device_index);
    if (!_device) {
        return false;
    }
    _device->AddRef();

    if (_device->QueryInterface(IID_IDeckLinkInput, (void **)&_input) != S_OK || !_input) {
        close();
        return false;
    }

    _display_mode = (BMDDisplayMode)p_display_mode;
    _input_flags = bmdVideoInputFlagDefault;

    IDeckLinkProfileAttributes *attributes = nullptr;
    if (_device->QueryInterface(IID_IDeckLinkProfileAttributes, (void **)&attributes) == S_OK && attributes) {
        bool supports_format_detection = false;
        if (attributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &supports_format_detection) == S_OK && supports_format_detection) {
            _input_flags = bmdVideoInputEnableFormatDetection;
        }
        attributes->Release();
    }

    bool supported_bgra = false;
    bool supported_yuv = false;
    BMDDisplayMode actual_mode = bmdModeUnknown;
    _input->DoesSupportVideoMode(bmdVideoConnectionUnspecified, _display_mode, bmdFormat8BitBGRA, bmdNoVideoInputConversion, bmdSupportedVideoModeDefault, &actual_mode, &supported_bgra);
    if (!supported_bgra) {
        _input->DoesSupportVideoMode(bmdVideoConnectionUnspecified, _display_mode, bmdFormat8BitYUV, bmdNoVideoInputConversion, bmdSupportedVideoModeDefault, &actual_mode, &supported_yuv);
    }

    if (!supported_bgra && !supported_yuv) {
        UtilityFunctions::printerr("[DeckLinkInput] Display mode is not supported for BGRA or YUV input.");
        close();
        return false;
    }

    _pixel_format = supported_bgra ? bmdFormat8BitBGRA : bmdFormat8BitYUV;
    if (actual_mode != bmdModeUnknown) {
        _display_mode = actual_mode;
    }

    IDeckLinkDisplayMode *mode = nullptr;
    if (_input->GetDisplayMode(_display_mode, &mode) != S_OK || !mode) {
        close();
        return false;
    }
    _width = mode->GetWidth();
    _height = mode->GetHeight();
    mode->Release();

    {
        std::lock_guard<std::mutex> lock(_frame_mutex);
        _latest_rgba.resize(_width * _height * 4);
        _has_frame = false;
    }

    HRESULT result = _input->SetCallback(this);
    if (result != S_OK) {
        UtilityFunctions::printerr("[DeckLinkInput] SetCallback failed: ", (int64_t)result);
        close();
        return false;
    }

    result = _input->EnableVideoInput(_display_mode, _pixel_format, _input_flags);
    if (result != S_OK) {
        UtilityFunctions::printerr("[DeckLinkInput] EnableVideoInput failed: ", (int64_t)result,
                " mode=", (int64_t)_display_mode,
                " pixel_format=", (int64_t)_pixel_format,
                " flags=", (int64_t)_input_flags);
        close();
        return false;
    }

    result = _input->StartStreams();
    if (result != S_OK) {
        UtilityFunctions::printerr("[DeckLinkInput] StartStreams failed: ", (int64_t)result);
        close();
        return false;
    }

    _open = true;
    return true;
}

void DeckLinkInput::close() {
    if (_input) {
        _input->StopStreams();
        _input->DisableVideoInput();
        _input->SetCallback(nullptr);
    }

    decklink::safe_release(_input);
    decklink::safe_release(_device);

    std::lock_guard<std::mutex> lock(_frame_mutex);
    _latest_rgba.clear();
    _has_frame = false;
    _open = false;
    _width = 0;
    _height = 0;
}

bool DeckLinkInput::is_open() const {
    return _open;
}

bool DeckLinkInput::has_frame() const {
    std::lock_guard<std::mutex> lock(_frame_mutex);
    return _has_frame;
}

Ref<Image> DeckLinkInput::get_image() const {
    std::lock_guard<std::mutex> lock(_frame_mutex);
    if (!_has_frame || _latest_rgba.is_empty()) {
        return Ref<Image>();
    }
    return Image::create_from_data(_width, _height, false, Image::FORMAT_RGBA8, _latest_rgba);
}

int DeckLinkInput::get_width() const {
    return _width;
}

int DeckLinkInput::get_height() const {
    return _height;
}

HRESULT DeckLinkInput::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents p_events, IDeckLinkDisplayMode *p_new_display_mode, BMDDetectedVideoInputFormatFlags p_flags) {
    if (!_input || !p_new_display_mode) {
        return S_OK;
    }

    if (p_events & (bmdVideoInputDisplayModeChanged | bmdVideoInputColorspaceChanged)) {
        _input->StopStreams();
        _input->FlushStreams();
        _display_mode = p_new_display_mode->GetDisplayMode();
        _width = p_new_display_mode->GetWidth();
        _height = p_new_display_mode->GetHeight();
        {
            std::lock_guard<std::mutex> lock(_frame_mutex);
            _latest_rgba.resize(_width * _height * 4);
            _has_frame = false;
        }
        _input->EnableVideoInput(_display_mode, _pixel_format, _input_flags);
        return _input->StartStreams();
    }

    return S_OK;
}

HRESULT DeckLinkInput::VideoInputFrameArrived(IDeckLinkVideoInputFrame *p_video_frame, IDeckLinkAudioInputPacket *p_audio_packet) {
    if (!p_video_frame || (p_video_frame->GetFlags() & bmdFrameHasNoInputSource)) {
        return S_OK;
    }

    PackedByteArray rgba;
    if (copy_frame_to_rgba(p_video_frame, rgba)) {
        std::lock_guard<std::mutex> lock(_frame_mutex);
        _width = p_video_frame->GetWidth();
        _height = p_video_frame->GetHeight();
        _latest_rgba = rgba;
        _has_frame = true;
    } else {
        UtilityFunctions::printerr("[DeckLinkInput] Could not convert frame to RGBA: pixel_format=", (int64_t)p_video_frame->GetPixelFormat());
    }
    return S_OK;
}
