#pragma once

#include <DeckLinkAPI.h>

#include <stdlib.h>
#include <string.h>

namespace godot {
namespace decklink {

inline void release_string(const char *p_string) {
    if (p_string) {
        free((void *)p_string);
    }
}

template <typename T>
inline void safe_release(T *&p_object) {
    if (p_object) {
        p_object->Release();
        p_object = nullptr;
    }
}

inline bool iid_equal(REFIID p_a, REFIID p_b) {
    return memcmp(&p_a, &p_b, sizeof(p_a)) == 0;
}

inline const char *hresult_name(HRESULT p_result) {
    switch (p_result) {
        case S_OK:
            return "S_OK";
        case S_FALSE:
            return "S_FALSE";
        case E_INVALIDARG:
            return "E_INVALIDARG";
        case E_ACCESSDENIED:
            return "E_ACCESSDENIED";
        case E_FAIL:
            return "E_FAIL";
        case E_NOINTERFACE:
            return "E_NOINTERFACE";
        case E_OUTOFMEMORY:
            return "E_OUTOFMEMORY";
        case E_NOTIMPL:
            return "E_NOTIMPL";
        default:
            return "UNKNOWN";
    }
}

} // namespace decklink
} // namespace godot
