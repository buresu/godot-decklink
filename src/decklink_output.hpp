#pragma once

#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/thread.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/templates/safe_refcount.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "decklink_common.hpp"
#include "decklink_device.hpp"

namespace godot {

class DeckLinkOutput : public Node, public IDeckLinkVideoOutputCallback {
    GDCLASS(DeckLinkOutput, Node)

public:
    enum OutputFormat {
        OUTPUT_FORMAT_AUTO = 0,
        OUTPUT_FORMAT_BGRA = 1,
        OUTPUT_FORMAT_YUV = 2,
    };

    DeckLinkOutput();
    ~DeckLinkOutput() override;

    void _ready() override;
    void _exit_tree() override;
    void _validate_property(PropertyInfo &p_property) const;

    HRESULT QueryInterface(REFIID p_iid, LPVOID *r_ppv) override;
    ULONG AddRef() override;
    ULONG Release() override;

    bool open(int p_device, int64_t p_display_mode = bmdModeHD1080p5994);
    void close();
    bool is_open() const;
    Ref<Texture2D> get_texture() const;
    void set_texture(Ref<Texture2D> p_texture);
    bool is_enabled() const;
    void set_enabled(bool p_enabled);
    int get_device() const;
    void set_device(int p_device);
    int64_t get_display_mode() const;
    void set_display_mode(int64_t p_display_mode);
    int64_t get_connection() const;
    void set_connection(int64_t p_connection);
    OutputFormat get_output_format() const;
    void set_output_format(OutputFormat p_output_format);
    int get_width() const;
    int get_height() const;

    HRESULT ScheduledFrameCompleted(IDeckLinkVideoFrame *p_completed_frame, BMDOutputFrameCompletionResult p_result) override;
    HRESULT ScheduledPlaybackHasStopped() override;

protected:
    static void _bind_methods();

private:
    bool _output_frame(const PackedByteArray &p_rgba);
    void _capture_texture();
    void _output_thread_loop();
    void _start_output_thread();
    void _stop_output_thread();
    void _on_frame_post_draw();
    void _connect_frame_post_draw();
    void _disconnect_frame_post_draw();
    bool _is_output_thread_stop_requested() const;
    void _restart_if_enabled();
    String _get_device_hint_string() const;
    String _get_display_mode_hint_string() const;

    SafeRefCount _ref_count;
    Ref<DeckLinkDevice> _decklink_device;
    IDeckLinkOutput *_output = nullptr;
    int _device = 0;
    BMDDisplayMode _display_mode = bmdModeHD1080p5994;
    BMDVideoConnection _connection = bmdVideoConnectionUnspecified;
    OutputFormat _output_format = OUTPUT_FORMAT_AUTO;
    BMDPixelFormat _pixel_format = bmdFormat8BitBGRA;
    BMDTimeValue _frame_duration = 1001;
    BMDTimeScale _time_scale = 60000;
    int _width = 0;
    int _height = 0;
    bool _open = false;
    bool _enabled = false;
    bool _frame_post_draw_connected = false;
    bool _output_thread_stop_requested = false;
    mutable Mutex *_output_mutex = nullptr;
    Ref<Thread> _output_thread;
    PackedByteArray _latest_rgba;
    bool _has_output_frame = false;
    Ref<Texture2D> _texture;
};

} // namespace godot

VARIANT_ENUM_CAST(DeckLinkOutput::OutputFormat);
