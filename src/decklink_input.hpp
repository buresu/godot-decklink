#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/templates/safe_refcount.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "decklink_common.hpp"
#include "decklink_device.hpp"

namespace godot {

class DeckLinkInput : public Node, public IDeckLinkInputCallback {
    GDCLASS(DeckLinkInput, Node)

public:
    DeckLinkInput();
    ~DeckLinkInput() override;

    void _ready() override;
    void _process(double p_delta) override;
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
    Ref<ImageTexture> get_texture() const;
    void set_texture(Ref<ImageTexture> p_texture);
    bool has_frame() const;
    int get_width() const;
    int get_height() const;

    HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents p_events, IDeckLinkDisplayMode *p_new_display_mode, BMDDetectedVideoInputFormatFlags p_flags) override;
    HRESULT VideoInputFrameArrived(IDeckLinkVideoInputFrame *p_video_frame, IDeckLinkAudioInputPacket *p_audio_packet) override;

protected:
    static void _bind_methods();

private:
    void _input_thread_loop();
    void _start_input_thread();
    void _stop_input_thread();
    bool _is_input_thread_stop_requested() const;
    void _clear_pending_frame();
    void _update_texture();
    void _restart_if_enabled();
    String _get_device_hint_string() const;
    String _get_display_mode_hint_string() const;

    SafeRefCount _ref_count;
    Ref<DeckLinkDevice> _decklink_device;
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
    bool _input_thread_stop_requested = false;
    Ref<Thread> _input_thread;
    IDeckLinkVideoInputFrame *_pending_frame = nullptr;
    PackedByteArray _latest_rgba;
    Ref<ImageTexture> _texture;
    bool _has_frame = false;
    bool _texture_dirty = false;
};

} // namespace godot
