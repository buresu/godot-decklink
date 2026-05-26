#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/templates/safe_refcount.hpp>
#include <godot_cpp/variant/string.hpp>

#include "decklink_common.hpp"

namespace godot {

class DeckLinkOutput : public Object, public IDeckLinkVideoOutputCallback {
    GDCLASS(DeckLinkOutput, Object)

public:
    DeckLinkOutput();
    ~DeckLinkOutput() override;

    HRESULT QueryInterface(REFIID p_iid, LPVOID *r_ppv) override;
    ULONG AddRef() override;
    ULONG Release() override;

    bool open(int p_device_index, int64_t p_display_mode = bmdModeHD1080p5994);
    void close();
    bool is_open() const;
    bool output_image(const Ref<Image> &p_image);
    int get_width() const;
    int get_height() const;

    HRESULT ScheduledFrameCompleted(IDeckLinkVideoFrame *p_completed_frame, BMDOutputFrameCompletionResult p_result) override;
    HRESULT ScheduledPlaybackHasStopped() override;

protected:
    static void _bind_methods();

private:
    SafeRefCount _ref_count;
    IDeckLink *_device = nullptr;
    IDeckLinkOutput *_output = nullptr;
    BMDDisplayMode _display_mode = bmdModeHD1080p5994;
    BMDPixelFormat _pixel_format = bmdFormat8BitBGRA;
    BMDTimeValue _frame_duration = 1001;
    BMDTimeScale _time_scale = 60000;
    BMDTimeValue _next_frame_time = 0;
    int _width = 0;
    int _height = 0;
    bool _open = false;
};

} // namespace godot
