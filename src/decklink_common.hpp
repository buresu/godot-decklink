#pragma once

#include <DeckLinkAPI.h>

#include <cstdlib>
#include <cstring>

namespace godot {
namespace decklink {

inline void release_string(const char *p_string) {
    if (p_string) {
        std::free((void *)p_string);
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
    return std::memcmp(&p_a, &p_b, sizeof(p_a)) == 0;
}

} // namespace decklink
} // namespace godot
