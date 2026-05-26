#include "decklink_output.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <libyuv.h>

#include "decklink.hpp"

#include <cstring>

using namespace godot;

DeckLinkOutput::DeckLinkOutput() = default;

DeckLinkOutput::~DeckLinkOutput() {
    close();
}

void DeckLinkOutput::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open", "device_index", "display_mode"), &DeckLinkOutput::open, DEFVAL((int64_t)bmdModeHD1080p5994));
    ClassDB::bind_method(D_METHOD("close"), &DeckLinkOutput::close);
    ClassDB::bind_method(D_METHOD("is_open"), &DeckLinkOutput::is_open);
    ClassDB::bind_method(D_METHOD("output_image", "image"), &DeckLinkOutput::output_image);
    ClassDB::bind_method(D_METHOD("get_width"), &DeckLinkOutput::get_width);
    ClassDB::bind_method(D_METHOD("get_height"), &DeckLinkOutput::get_height);
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
    return ++_ref_count;
}

ULONG DeckLinkOutput::Release() {
    const ULONG count = --_ref_count;
    return count;
}

bool DeckLinkOutput::open(int p_device_index, int64_t p_display_mode) {
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

    if (_device->QueryInterface(IID_IDeckLinkOutput, (void **)&_output) != S_OK || !_output) {
        close();
        return false;
    }

    _display_mode = (BMDDisplayMode)p_display_mode;

    bool supported_bgra = false;
    bool supported_yuv = false;
    BMDDisplayMode actual_mode = bmdModeUnknown;
    _output->DoesSupportVideoMode(bmdVideoConnectionUnspecified, _display_mode, bmdFormat8BitBGRA, bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault, &actual_mode, &supported_bgra);
    if (!supported_bgra) {
        _output->DoesSupportVideoMode(bmdVideoConnectionUnspecified, _display_mode, bmdFormat8BitYUV, bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault, &actual_mode, &supported_yuv);
    }

    if (!supported_bgra && !supported_yuv) {
        UtilityFunctions::printerr("[DeckLinkOutput] Display mode is not supported for BGRA or YUV output.");
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

    _next_frame_time = 0;
    _open = true;
    return true;
}

void DeckLinkOutput::close() {
    if (_output) {
        _output->DisableVideoOutput();
    }

    decklink::safe_release(_output);
    decklink::safe_release(_device);
    _open = false;
    _width = 0;
    _height = 0;
    _next_frame_time = 0;
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

bool DeckLinkOutput::output_image(const Ref<Image> &p_image) {
    if (!_open || !_output || p_image.is_null()) {
        return false;
    }

    Ref<Image> image = p_image->duplicate();
    if (image->get_width() != _width || image->get_height() != _height) {
        image->resize(_width, _height);
    }
    if (image->get_format() != Image::FORMAT_RGBA8) {
        image->convert(Image::FORMAT_RGBA8);
    }

    PackedByteArray data = image->get_data();
    const uint8_t *src = data.ptr();

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

HRESULT DeckLinkOutput::ScheduledFrameCompleted(IDeckLinkVideoFrame *p_completed_frame, BMDOutputFrameCompletionResult p_result) {
    return S_OK;
}

HRESULT DeckLinkOutput::ScheduledPlaybackHasStopped() {
    return S_OK;
}
