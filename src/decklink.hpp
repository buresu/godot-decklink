#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "decklink_common.hpp"
#include "decklink_device.hpp"

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

  Ref<DeckLinkDevice> get_device(int p_index) const;

protected:
  static void _bind_methods();

private:
  static DeckLink *_singleton;
  Vector<Ref<DeckLinkDevice>> _devices;

  void _clear_devices();
};

} // namespace godot
