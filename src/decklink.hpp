#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "decklink_common.hpp"

namespace godot {

class DeckLink : public Object {
    GDCLASS(DeckLink, Object)

public:
    static DeckLink *get_singleton();

    DeckLink();
    ~DeckLink() override;

    int get_device_count() const;
    Array get_devices() const;
    Array get_output_display_modes(int p_device_index) const;
    Array get_input_display_modes(int p_device_index) const;
    void refresh();

    IDeckLink *get_device(int p_index) const;

protected:
    static void _bind_methods();

private:
    static DeckLink *_singleton;
    Vector<IDeckLink *> _devices;

    Array _get_display_modes(int p_device_index, bool p_output) const;
    void _clear_devices();
};

} // namespace godot
