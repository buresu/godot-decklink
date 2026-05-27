#include "decklink.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

DeckLink *DeckLink::_singleton = nullptr;

DeckLink::DeckLink() {
  _singleton = this;
  refresh();
}

DeckLink::~DeckLink() {
  _clear_devices();
  _singleton = nullptr;
}

DeckLink *DeckLink::get_singleton() { return _singleton; }

void DeckLink::_bind_methods() {
  ClassDB::bind_method(D_METHOD("refresh"), &DeckLink::refresh);
  ClassDB::bind_method(D_METHOD("get_device_count"),
                       &DeckLink::get_device_count);
  ClassDB::bind_method(D_METHOD("get_devices"), &DeckLink::get_devices);
  ClassDB::bind_method(D_METHOD("get_device", "device_index"),
                       &DeckLink::get_device);
  ClassDB::bind_method(D_METHOD("get_output_display_modes", "device_index"),
                       &DeckLink::get_output_display_modes);
  ClassDB::bind_method(D_METHOD("get_input_display_modes", "device_index"),
                       &DeckLink::get_input_display_modes);
}

void DeckLink::_clear_devices() { _devices.clear(); }

void DeckLink::refresh() {
  _clear_devices();

  IDeckLinkIterator *iterator = decklink::create_iterator();
  if (!iterator) {
    UtilityFunctions::printerr("[DeckLink] Could not create DeckLink iterator. "
                               "Is Blackmagic Desktop Video installed?");
    return;
  }

  IDeckLink *device = nullptr;
  while (iterator->Next(&device) == S_OK) {
    Ref<DeckLinkDevice> decklink_device;
    decklink_device.instantiate();
    decklink_device->setup(device);
    _devices.push_back(decklink_device);
    device->Release();
    device = nullptr;
  }

  iterator->Release();
}

int DeckLink::get_device_count() const { return _devices.size(); }

Array DeckLink::get_devices() const {
  Array devices;

  for (int i = 0; i < _devices.size(); ++i) {
    Dictionary device = _devices[i]->to_dictionary();
    device["index"] = i;
    devices.push_back(device);
  }

  return devices;
}

Array DeckLink::get_output_display_modes(int p_device_index) const {
  Ref<DeckLinkDevice> device = get_device(p_device_index);
  return device.is_valid() ? device->get_output_display_modes() : Array();
}

Array DeckLink::get_input_display_modes(int p_device_index) const {
  Ref<DeckLinkDevice> device = get_device(p_device_index);
  return device.is_valid() ? device->get_input_display_modes() : Array();
}

Ref<DeckLinkDevice> DeckLink::get_device(int p_index) const {
  if (p_index < 0 || p_index >= _devices.size()) {
    return Ref<DeckLinkDevice>();
  }
  return _devices[p_index];
}
