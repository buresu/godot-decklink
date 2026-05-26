#pragma once

#include <DeckLinkAPI.h>

#include <atomic>
#include <vector>

namespace godot {

class DeckLinkMemoryVideoFrame : public IDeckLinkVideoFrame, public IDeckLinkVideoBuffer {
public:
    DeckLinkMemoryVideoFrame(int p_width, int p_height, BMDPixelFormat p_format);

    HRESULT QueryInterface(REFIID p_iid, LPVOID *r_ppv) override;
    ULONG AddRef() override;
    ULONG Release() override;

    long GetWidth() override;
    long GetHeight() override;
    long GetRowBytes() override;
    BMDPixelFormat GetPixelFormat() override;
    BMDFrameFlags GetFlags() override;
    HRESULT GetTimecode(BMDTimecodeFormat p_format, IDeckLinkTimecode **r_timecode) override;
    HRESULT GetAncillaryData(IDeckLinkVideoFrameAncillary **r_ancillary) override;

    HRESULT GetBytes(void **r_buffer) override;
    HRESULT GetSize(uint64_t *r_size) override;
    HRESULT StartAccess(BMDBufferAccessFlags p_flags) override;
    HRESULT EndAccess(BMDBufferAccessFlags p_flags) override;

    const uint8_t *data() const;
    int size() const;

protected:
    ~DeckLinkMemoryVideoFrame() override = default;

private:
    std::atomic<ULONG> _ref_count = 1;
    int _width = 0;
    int _height = 0;
    int _row_bytes = 0;
    BMDPixelFormat _format = bmdFormatUnspecified;
    std::vector<uint8_t> _data;
};

} // namespace godot
