#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "decklink_common.hpp"

namespace godot {

class DeckLinkDevice : public RefCounted {
    GDCLASS(DeckLinkDevice, RefCounted)

public:
    DeckLinkDevice();
    ~DeckLinkDevice() override;

    void setup(IDeckLink *p_device);

    String get_model_name() const;
    String get_display_name() const;
    Dictionary to_dictionary() const;
    Array get_input_display_modes() const;
    Array get_output_display_modes() const;

    IDeckLink *get_decklink() const;
    bool query_input(IDeckLinkInput **r_input) const;
    bool query_output(IDeckLinkOutput **r_output) const;
    bool query_attributes(IDeckLinkProfileAttributes **r_attributes) const;

protected:
    static void _bind_methods();

private:
    Array _get_display_modes(bool p_output) const;
    String _read_model_name() const;
    String _read_display_name() const;

    IDeckLink *_device = nullptr;
};

} // namespace godot
