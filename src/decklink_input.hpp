#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/templates/safe_refcount.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include "decklink_common.hpp"

namespace godot {

class DeckLinkInput : public Object, public IDeckLinkInputCallback {
    GDCLASS(DeckLinkInput, Object)

public:
    DeckLinkInput();
    ~DeckLinkInput() override;

    HRESULT QueryInterface(REFIID p_iid, LPVOID *r_ppv) override;
    ULONG AddRef() override;
    ULONG Release() override;

    bool open(int p_device_index, int64_t p_display_mode = bmdModeHD1080p5994);
    void close();
    bool is_open() const;
    bool has_frame() const;
    Ref<Image> get_image() const;
    int get_width() const;
    int get_height() const;

    HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents p_events, IDeckLinkDisplayMode *p_new_display_mode, BMDDetectedVideoInputFormatFlags p_flags) override;
    HRESULT VideoInputFrameArrived(IDeckLinkVideoInputFrame *p_video_frame, IDeckLinkAudioInputPacket *p_audio_packet) override;

protected:
    static void _bind_methods();

private:
    SafeRefCount _ref_count;
    IDeckLink *_device = nullptr;
    IDeckLinkInput *_input = nullptr;
    BMDDisplayMode _display_mode = bmdModeHD1080p5994;
    BMDPixelFormat _pixel_format = bmdFormat8BitYUV;
    BMDVideoInputFlags _input_flags = bmdVideoInputFlagDefault;
    int _width = 0;
    int _height = 0;
    bool _open = false;
    mutable Mutex _frame_mutex;
    PackedByteArray _latest_rgba;
    bool _has_frame = false;
};

} // namespace godot
