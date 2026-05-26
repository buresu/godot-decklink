#include "decklink_output.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

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

    bool supported = false;
    BMDDisplayMode actual_mode = bmdModeUnknown;
    if (_output->DoesSupportVideoMode(bmdVideoConnectionUnspecified, _display_mode, bmdFormat8BitBGRA, bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault, &actual_mode, &supported) != S_OK || !supported) {
        UtilityFunctions::printerr("[DeckLinkOutput] Display mode or BGRA output is not supported.");
        close();
        return false;
    }
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

    if (_output->SetScheduledFrameCompletionCallback(this) != S_OK ||
            _output->EnableVideoOutput(_display_mode, bmdVideoOutputFlagDefault) != S_OK ||
            _output->StartScheduledPlayback(0, _time_scale, 1.0) != S_OK) {
        close();
        return false;
    }

    _next_frame_time = 0;
    _open = true;
    return true;
}

void DeckLinkOutput::close() {
    if (_output) {
        _output->StopScheduledPlayback(0, nullptr, _time_scale);
        _output->DisableVideoOutput();
        _output->SetScheduledFrameCompletionCallback(nullptr);
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

    IDeckLinkMutableVideoFrame *frame = nullptr;
    if (_output->CreateVideoFrame(_width, _height, _width * 4, bmdFormat8BitBGRA, bmdFrameFlagDefault, &frame) != S_OK || !frame) {
        return false;
    }

    IDeckLinkVideoBuffer *buffer = nullptr;
    if (frame->QueryInterface(IID_IDeckLinkVideoBuffer, (void **)&buffer) != S_OK || !buffer) {
        frame->Release();
        return false;
    }

    void *frame_bytes = nullptr;
    bool ok = false;
    if (buffer->StartAccess(bmdBufferAccessWrite) == S_OK && buffer->GetBytes(&frame_bytes) == S_OK && frame_bytes) {
        uint8_t *dst = static_cast<uint8_t *>(frame_bytes);
        const int pixels = _width * _height;
        for (int i = 0; i < pixels; ++i) {
            dst[i * 4 + 0] = src[i * 4 + 2];
            dst[i * 4 + 1] = src[i * 4 + 1];
            dst[i * 4 + 2] = src[i * 4 + 0];
            dst[i * 4 + 3] = src[i * 4 + 3];
        }
        ok = _output->ScheduleVideoFrame(frame, _next_frame_time, _frame_duration, _time_scale) == S_OK;
        if (ok) {
            _next_frame_time += _frame_duration;
        }
    }
    buffer->EndAccess(bmdBufferAccessWrite);
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
