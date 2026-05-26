#include "decklink_video_frame.hpp"

#include "decklink_common.hpp"

using namespace godot;

DeckLinkVideoFrame::DeckLinkVideoFrame(int p_width, int p_height, BMDPixelFormat p_format) {
    _ref_count.init();
    _width = p_width;
    _height = p_height;
    _format = p_format;
    _row_bytes = _width * 4;
    _data.resize(_row_bytes * _height);
}

HRESULT DeckLinkVideoFrame::QueryInterface(REFIID p_iid, LPVOID *r_ppv) {
    if (!r_ppv) {
        return E_INVALIDARG;
    }
    if (decklink::iid_equal(p_iid, IID_IUnknown)) {
        *r_ppv = static_cast<IDeckLinkVideoFrame *>(this);
    } else if (decklink::iid_equal(p_iid, IID_IDeckLinkVideoFrame)) {
        *r_ppv = static_cast<IDeckLinkVideoFrame *>(this);
    } else if (decklink::iid_equal(p_iid, IID_IDeckLinkVideoBuffer)) {
        *r_ppv = static_cast<IDeckLinkVideoBuffer *>(this);
    } else {
        *r_ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

ULONG DeckLinkVideoFrame::AddRef() {
    return _ref_count.refval();
}

ULONG DeckLinkVideoFrame::Release() {
    const ULONG count = _ref_count.unrefval();
    if (count == 0) {
        delete this;
    }
    return count;
}

long DeckLinkVideoFrame::GetWidth() {
    return _width;
}

long DeckLinkVideoFrame::GetHeight() {
    return _height;
}

long DeckLinkVideoFrame::GetRowBytes() {
    return _row_bytes;
}

BMDPixelFormat DeckLinkVideoFrame::GetPixelFormat() {
    return _format;
}

BMDFrameFlags DeckLinkVideoFrame::GetFlags() {
    return bmdFrameFlagDefault;
}

HRESULT DeckLinkVideoFrame::GetTimecode(BMDTimecodeFormat p_format, IDeckLinkTimecode **r_timecode) {
    if (r_timecode) {
        *r_timecode = nullptr;
    }
    return E_NOINTERFACE;
}

HRESULT DeckLinkVideoFrame::GetAncillaryData(IDeckLinkVideoFrameAncillary **r_ancillary) {
    if (r_ancillary) {
        *r_ancillary = nullptr;
    }
    return E_NOINTERFACE;
}

HRESULT DeckLinkVideoFrame::GetBytes(void **r_buffer) {
    if (!r_buffer) {
        return E_INVALIDARG;
    }
    *r_buffer = _data.ptrw();
    return S_OK;
}

HRESULT DeckLinkVideoFrame::GetSize(uint64_t *r_size) {
    if (!r_size) {
        return E_INVALIDARG;
    }
    *r_size = _data.size();
    return S_OK;
}

HRESULT DeckLinkVideoFrame::StartAccess(BMDBufferAccessFlags p_flags) {
    return S_OK;
}

HRESULT DeckLinkVideoFrame::EndAccess(BMDBufferAccessFlags p_flags) {
    return S_OK;
}

const uint8_t *DeckLinkVideoFrame::data() const {
    return _data.ptr();
}

int DeckLinkVideoFrame::size() const {
    return (int)_data.size();
}
