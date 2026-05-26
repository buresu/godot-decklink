#include "decklink_input.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "decklink.hpp"
#include "decklink_memory_video_frame.hpp"

using namespace godot;

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

    bool supported = false;
    BMDDisplayMode actual_mode = bmdModeUnknown;
    if (_input->DoesSupportVideoMode(bmdVideoConnectionUnspecified, _display_mode, bmdFormat8BitYUV, bmdNoVideoInputConversion, bmdSupportedVideoModeDefault, &actual_mode, &supported) != S_OK || !supported) {
        UtilityFunctions::printerr("[DeckLinkInput] Display mode or YUV input is not supported.");
        close();
        return false;
    }
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

    if (_input->SetCallback(this) != S_OK ||
            _input->EnableVideoInput(_display_mode, bmdFormat8BitYUV, bmdVideoInputEnableFormatDetection) != S_OK ||
            _input->StartStreams() != S_OK) {
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
        _input->EnableVideoInput(_display_mode, bmdFormat8BitYUV, bmdVideoInputEnableFormatDetection);
        return _input->StartStreams();
    }

    return S_OK;
}

HRESULT DeckLinkInput::VideoInputFrameArrived(IDeckLinkVideoInputFrame *p_video_frame, IDeckLinkAudioInputPacket *p_audio_packet) {
    if (!p_video_frame || !_converter || (p_video_frame->GetFlags() & bmdFrameHasNoInputSource)) {
        return S_OK;
    }

    const int width = p_video_frame->GetWidth();
    const int height = p_video_frame->GetHeight();
    DeckLinkMemoryVideoFrame *bgra_frame = new DeckLinkMemoryVideoFrame(width, height, bmdFormat8BitBGRA);

    if (_converter->ConvertFrame(p_video_frame, bgra_frame) == S_OK) {
        PackedByteArray rgba;
        rgba.resize(width * height * 4);
        uint8_t *dst = rgba.ptrw();
        const uint8_t *src = bgra_frame->data();
        const int pixels = width * height;
        for (int i = 0; i < pixels; ++i) {
            dst[i * 4 + 0] = src[i * 4 + 2];
            dst[i * 4 + 1] = src[i * 4 + 1];
            dst[i * 4 + 2] = src[i * 4 + 0];
            dst[i * 4 + 3] = src[i * 4 + 3];
        }

        std::lock_guard<std::mutex> lock(_frame_mutex);
        _width = width;
        _height = height;
        _latest_rgba = rgba;
        _has_frame = true;
    }

    bgra_frame->Release();
    return S_OK;
}
