#include "decklink_device.hpp"

#include <godot_cpp/core/class_db.hpp>

using namespace godot;

DeckLinkDevice::DeckLinkDevice() {
}

DeckLinkDevice::~DeckLinkDevice() {
    decklink::safe_release(_device);
}

void DeckLinkDevice::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_model_name"), &DeckLinkDevice::get_model_name);
    ClassDB::bind_method(D_METHOD("get_display_name"), &DeckLinkDevice::get_display_name);
    ClassDB::bind_method(D_METHOD("to_dictionary"), &DeckLinkDevice::to_dictionary);
    ClassDB::bind_method(D_METHOD("get_input_display_modes"), &DeckLinkDevice::get_input_display_modes);
    ClassDB::bind_method(D_METHOD("get_output_display_modes"), &DeckLinkDevice::get_output_display_modes);
}

void DeckLinkDevice::setup(IDeckLink *p_device) {
    decklink::safe_release(_device);
    _device = p_device;
    if (_device) {
        _device->AddRef();
    }
}

String DeckLinkDevice::get_model_name() const {
    return _read_model_name();
}

String DeckLinkDevice::get_display_name() const {
    return _read_display_name();
}

Dictionary DeckLinkDevice::to_dictionary() const {
    Dictionary item;
    item["model_name"] = get_model_name();
    item["display_name"] = get_display_name();
    return item;
}

Array DeckLinkDevice::get_input_display_modes() const {
    return _get_display_modes(false);
}

Array DeckLinkDevice::get_output_display_modes() const {
    return _get_display_modes(true);
}

IDeckLink *DeckLinkDevice::get_decklink() const {
    return _device;
}

bool DeckLinkDevice::query_input(IDeckLinkInput **r_input) const {
    if (r_input) {
        *r_input = nullptr;
    }
    return _device && r_input && _device->QueryInterface(IID_IDeckLinkInput, (void **)r_input) == S_OK && *r_input;
}

bool DeckLinkDevice::query_output(IDeckLinkOutput **r_output) const {
    if (r_output) {
        *r_output = nullptr;
    }
    return _device && r_output && _device->QueryInterface(IID_IDeckLinkOutput, (void **)r_output) == S_OK && *r_output;
}

bool DeckLinkDevice::query_attributes(IDeckLinkProfileAttributes **r_attributes) const {
    if (r_attributes) {
        *r_attributes = nullptr;
    }
    return _device && r_attributes && _device->QueryInterface(IID_IDeckLinkProfileAttributes, (void **)r_attributes) == S_OK && *r_attributes;
}

Array DeckLinkDevice::_get_display_modes(bool p_output) const {
    Array modes;

    IDeckLinkDisplayModeIterator *iterator = nullptr;
    if (p_output) {
        IDeckLinkOutput *output = nullptr;
        if (!query_output(&output)) {
            return modes;
        }
        output->GetDisplayModeIterator(&iterator);
        output->Release();
    } else {
        IDeckLinkInput *input = nullptr;
        if (!query_input(&input)) {
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

String DeckLinkDevice::_read_model_name() const {
    if (!_device) {
        return String();
    }

    const char *model_name = nullptr;
    if (_device->GetModelName(&model_name) == S_OK && model_name) {
        const String result = String::utf8(model_name);
        decklink::release_string(model_name);
        return result;
    }
    return String();
}

String DeckLinkDevice::_read_display_name() const {
    if (!_device) {
        return String();
    }

    const char *display_name = nullptr;
    if (_device->GetDisplayName(&display_name) == S_OK && display_name) {
        const String result = String::utf8(display_name);
        decklink::release_string(display_name);
        return result;
    }
    return String();
}
