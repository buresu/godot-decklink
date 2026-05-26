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

DeckLink *DeckLink::get_singleton() {
    return _singleton;
}

void DeckLink::_bind_methods() {
    ClassDB::bind_method(D_METHOD("refresh"), &DeckLink::refresh);
    ClassDB::bind_method(D_METHOD("get_device_count"), &DeckLink::get_device_count);
    ClassDB::bind_method(D_METHOD("get_devices"), &DeckLink::get_devices);
    ClassDB::bind_method(D_METHOD("get_output_display_modes", "device_index"), &DeckLink::get_output_display_modes);
    ClassDB::bind_method(D_METHOD("get_input_display_modes", "device_index"), &DeckLink::get_input_display_modes);
}

void DeckLink::_clear_devices() {
    for (int i = 0; i < _devices.size(); ++i) {
        IDeckLink *device = _devices[i];
        decklink::safe_release(device);
    }
    _devices.clear();
}

void DeckLink::refresh() {
    _clear_devices();

    IDeckLinkIterator *iterator = CreateDeckLinkIteratorInstance();
    if (!iterator) {
        UtilityFunctions::printerr("[DeckLink] Could not create DeckLink iterator. Is Blackmagic Desktop Video installed?");
        return;
    }

    IDeckLink *device = nullptr;
    while (iterator->Next(&device) == S_OK) {
        _devices.push_back(device);
        device = nullptr;
    }

    iterator->Release();
}

int DeckLink::get_device_count() const {
    return _devices.size();
}

Array DeckLink::get_devices() const {
    Array devices;

    for (int i = 0; i < _devices.size(); ++i) {
        IDeckLink *device = _devices[i];
        const char *model_name = nullptr;
        const char *display_name = nullptr;

        Dictionary item;
        item["index"] = i;

        if (device->GetModelName(&model_name) == S_OK && model_name) {
            item["model_name"] = String::utf8(model_name);
            decklink::release_string(model_name);
        } else {
            item["model_name"] = String();
        }

        if (device->GetDisplayName(&display_name) == S_OK && display_name) {
            item["display_name"] = String::utf8(display_name);
            decklink::release_string(display_name);
        } else {
            item["display_name"] = String();
        }

        devices.push_back(item);
    }

    return devices;
}

Array DeckLink::get_output_display_modes(int p_device_index) const {
    return _get_display_modes(p_device_index, true);
}

Array DeckLink::get_input_display_modes(int p_device_index) const {
    return _get_display_modes(p_device_index, false);
}

IDeckLink *DeckLink::get_device(int p_index) const {
    if (p_index < 0 || p_index >= _devices.size()) {
        return nullptr;
    }
    return _devices[p_index];
}

Array DeckLink::_get_display_modes(int p_device_index, bool p_output) const {
    Array modes;
    IDeckLink *device = get_device(p_device_index);
    if (!device) {
        return modes;
    }

    IDeckLinkDisplayModeIterator *iterator = nullptr;
    if (p_output) {
        IDeckLinkOutput *output = nullptr;
        if (device->QueryInterface(IID_IDeckLinkOutput, (void **)&output) != S_OK || !output) {
            return modes;
        }
        output->GetDisplayModeIterator(&iterator);
        output->Release();
    } else {
        IDeckLinkInput *input = nullptr;
        if (device->QueryInterface(IID_IDeckLinkInput, (void **)&input) != S_OK || !input) {
            return modes;
        }
        input->GetDisplayModeIterator(&iterator);
        input->Release();
    }

    if (!iterator) {
        return modes;
    }

    IDeckLinkDisplayMode *display_mode = nullptr;
    while (iterator->Next(&display_mode) == S_OK) {
        const char *name = nullptr;
        BMDTimeValue frame_duration = 0;
        BMDTimeScale time_scale = 0;

        Dictionary mode;
        mode["id"] = (int64_t)display_mode->GetDisplayMode();
        mode["width"] = display_mode->GetWidth();
        mode["height"] = display_mode->GetHeight();

        if (display_mode->GetName(&name) == S_OK && name) {
            mode["name"] = String::utf8(name);
            decklink::release_string(name);
        } else {
            mode["name"] = String();
        }

        if (display_mode->GetFrameRate(&frame_duration, &time_scale) == S_OK && frame_duration != 0) {
            mode["fps"] = (double)time_scale / (double)frame_duration;
        } else {
            mode["fps"] = 0.0;
        }

        modes.push_back(mode);
        display_mode->Release();
        display_mode = nullptr;
    }

    iterator->Release();
    return modes;
}
