#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/templates/safe_refcount.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "decklink_common.hpp"

namespace godot {

class DeckLinkInput : public Node, public IDeckLinkInputCallback {
    GDCLASS(DeckLinkInput, Node)

public:
    DeckLinkInput();
    ~DeckLinkInput() override;

    void _ready() override;
    void _exit_tree() override;
    void _validate_property(PropertyInfo &p_property) const;

    HRESULT QueryInterface(REFIID p_iid, LPVOID *r_ppv) override;
    ULONG AddRef() override;
    ULONG Release() override;

    bool open(int p_device, int64_t p_display_mode = bmdModeHD1080p5994);
    void close();
    bool is_open() const;
    bool is_enabled() const;
    void set_enabled(bool p_enabled);
    int get_device() const;
    void set_device(int p_device);
    int64_t get_display_mode() const;
    void set_display_mode(int64_t p_display_mode);
    bool has_frame() const;
    Ref<Image> get_image() const;
    int get_width() const;
    int get_height() const;

    HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents p_events, IDeckLinkDisplayMode *p_new_display_mode, BMDDetectedVideoInputFormatFlags p_flags) override;
    HRESULT VideoInputFrameArrived(IDeckLinkVideoInputFrame *p_video_frame, IDeckLinkAudioInputPacket *p_audio_packet) override;

protected:
    static void _bind_methods();

private:
    void _restart_if_enabled();
    String _get_device_hint_string() const;
    String _get_display_mode_hint_string() const;

    SafeRefCount _ref_count;
    IDeckLink *_decklink_device = nullptr;
    IDeckLinkInput *_decklink_input = nullptr;
    int _device = 0;
    BMDDisplayMode _display_mode = bmdModeHD1080p5994;
    BMDPixelFormat _pixel_format = bmdFormat8BitYUV;
    BMDVideoInputFlags _input_flags = bmdVideoInputFlagDefault;
    int _width = 0;
    int _height = 0;
    bool _open = false;
    bool _enabled = false;
    mutable Mutex *_frame_mutex = nullptr;
    PackedByteArray _latest_rgba;
    bool _has_frame = false;
};

} // namespace godot
