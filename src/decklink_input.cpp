#include "decklink_input.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "decklink.hpp"

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

static void yuv_to_rgb(uint8_t p_y, uint8_t p_u, uint8_t p_v, uint8_t &r_r, uint8_t &r_g, uint8_t &r_b) {
    const int c = (int)p_y - 16;
    const int d = (int)p_u - 128;
    const int e = (int)p_v - 128;
    r_r = clamp_u8((298 * c + 409 * e + 128) >> 8);
    r_g = clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
    r_b = clamp_u8((298 * c + 516 * d + 128) >> 8);
}

static bool copy_bgra_frame_to_rgba(IDeckLinkVideoFrame *p_frame, PackedByteArray &r_rgba) {
    if (!p_frame || p_frame->GetPixelFormat() != bmdFormat8BitBGRA) {
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

        for (int y = 0; y < height; ++y) {
            const uint8_t *src_row = src + y * row_bytes;
            uint8_t *dst_row = dst + y * width * 4;
            for (int x = 0; x < width; ++x) {
                dst_row[x * 4 + 0] = src_row[x * 4 + 2];
                dst_row[x * 4 + 1] = src_row[x * 4 + 1];
                dst_row[x * 4 + 2] = src_row[x * 4 + 0];
                dst_row[x * 4 + 3] = src_row[x * 4 + 3];
            }
        }

        ok = true;
    }

    if (access_started) {
        buffer->EndAccess(bmdBufferAccessRead);
    }
    buffer->Release();
    return ok;
}

static bool copy_yuv_frame_to_rgba(IDeckLinkVideoFrame *p_frame, PackedByteArray &r_rgba) {
    if (!p_frame || p_frame->GetPixelFormat() != bmdFormat8BitYUV) {
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

        for (int y = 0; y < height; ++y) {
            const uint8_t *src_row = src + y * row_bytes;
            uint8_t *dst_row = dst + y * width * 4;
            for (int x = 0; x + 1 < width; x += 2) {
                const uint8_t u = src_row[x * 2 + 0];
                const uint8_t y0 = src_row[x * 2 + 1];
                const uint8_t v = src_row[x * 2 + 2];
                const uint8_t y1 = src_row[x * 2 + 3];

                uint8_t r = 0;
                uint8_t g = 0;
                uint8_t b = 0;
                yuv_to_rgb(y0, u, v, r, g, b);
                dst_row[x * 4 + 0] = r;
                dst_row[x * 4 + 1] = g;
                dst_row[x * 4 + 2] = b;
                dst_row[x * 4 + 3] = 255;

                yuv_to_rgb(y1, u, v, r, g, b);
                dst_row[(x + 1) * 4 + 0] = r;
                dst_row[(x + 1) * 4 + 1] = g;
                dst_row[(x + 1) * 4 + 2] = b;
                dst_row[(x + 1) * 4 + 3] = 255;
            }
        }

        ok = true;
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

    _converter = CreateVideoConversionInstance();
    if (!_converter) {
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

    decklink::safe_release(_converter);
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
    if (!p_video_frame || !_converter || (p_video_frame->GetFlags() & bmdFrameHasNoInputSource)) {
        return S_OK;
    }

    IDeckLinkVideoFrame *bgra_frame = nullptr;
    if (p_video_frame->GetPixelFormat() == bmdFormat8BitBGRA) {
        bgra_frame = p_video_frame;
        bgra_frame->AddRef();
    } else {
        HRESULT result = _converter->ConvertNewFrame(p_video_frame, bmdFormat8BitBGRA, bmdColorspaceUnknown, nullptr, &bgra_frame);
        if (result != S_OK || !bgra_frame) {
            PackedByteArray rgba;
            if (!copy_yuv_frame_to_rgba(p_video_frame, rgba)) {
                UtilityFunctions::printerr("[DeckLinkInput] Frame conversion failed: ", (int64_t)result);
                return S_OK;
            }

            std::lock_guard<std::mutex> lock(_frame_mutex);
            _width = p_video_frame->GetWidth();
            _height = p_video_frame->GetHeight();
            _latest_rgba = rgba;
            _has_frame = true;
            return S_OK;
        }
    }

    PackedByteArray rgba;
    if (copy_bgra_frame_to_rgba(bgra_frame, rgba)) {
        std::lock_guard<std::mutex> lock(_frame_mutex);
        _width = bgra_frame->GetWidth();
        _height = bgra_frame->GetHeight();
        _latest_rgba = rgba;
        _has_frame = true;
    } else {
        UtilityFunctions::printerr("[DeckLinkInput] Could not read BGRA frame buffer.");
    }

    bgra_frame->Release();
    return S_OK;
}
