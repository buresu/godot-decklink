#include "decklink_output.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "decklink.hpp"

#include <cstring>

using namespace godot;

static uint8_t clamp_u8(int p_value) {
    if (p_value < 0) {
        return 0;
    }
    if (p_value > 255) {
        return 255;
    }
    return (uint8_t)p_value;
}

static void rgb_to_yuv(uint8_t p_r, uint8_t p_g, uint8_t p_b, uint8_t &r_y, uint8_t &r_u, uint8_t &r_v) {
    r_y = clamp_u8(((66 * p_r + 129 * p_g + 25 * p_b + 128) >> 8) + 16);
    r_u = clamp_u8(((-38 * p_r - 74 * p_g + 112 * p_b + 128) >> 8) + 128);
    r_v = clamp_u8(((112 * p_r - 94 * p_g - 18 * p_b + 128) >> 8) + 128);
}

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
        if (_pixel_format == bmdFormat8BitBGRA) {
            const int pixels = _width * _height;
            for (int i = 0; i < pixels; ++i) {
                dst[i * 4 + 0] = src[i * 4 + 2];
                dst[i * 4 + 1] = src[i * 4 + 1];
                dst[i * 4 + 2] = src[i * 4 + 0];
                dst[i * 4 + 3] = src[i * 4 + 3];
            }
        } else {
            for (int y = 0; y < _height; ++y) {
                const uint8_t *src_row = src + y * _width * 4;
                uint8_t *dst_row = dst + y * row_bytes;
                for (int x = 0; x + 1 < _width; x += 2) {
                    uint8_t y0 = 0;
                    uint8_t u0 = 0;
                    uint8_t v0 = 0;
                    uint8_t y1 = 0;
                    uint8_t u1 = 0;
                    uint8_t v1 = 0;
                    rgb_to_yuv(src_row[x * 4 + 0], src_row[x * 4 + 1], src_row[x * 4 + 2], y0, u0, v0);
                    rgb_to_yuv(src_row[(x + 1) * 4 + 0], src_row[(x + 1) * 4 + 1], src_row[(x + 1) * 4 + 2], y1, u1, v1);
                    dst_row[x * 2 + 0] = (uint8_t)(((int)u0 + (int)u1) / 2);
                    dst_row[x * 2 + 1] = y0;
                    dst_row[x * 2 + 2] = (uint8_t)(((int)v0 + (int)v1) / 2);
                    dst_row[x * 2 + 3] = y1;
                }
            }
        }
        result = _output->DisplayVideoFrameSync(frame);
        ok = result == S_OK;
        if (!ok) {
            UtilityFunctions::printerr("[DeckLinkOutput] DisplayVideoFrameSync failed: ",
                    decklink::hresult_name(result), " (", (int64_t)result, ")");
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
