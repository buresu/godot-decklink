#pragma once

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <DeckLinkAPI.h>

#include <godot_cpp/variant/string.hpp>

#include <stdlib.h>
#include <string.h>
#include <vector>

namespace godot {
namespace decklink {

#if defined(_WIN32)
using String = BSTR;
using Bool = BOOL;

inline ::godot::String string_to_godot(String p_string) {
  return p_string ? ::godot::String((const wchar_t *)p_string)
                  : ::godot::String();
}

inline void release_string(String p_string) {
  if (p_string) {
    SysFreeString(p_string);
  }
}

inline IDeckLinkIterator *create_iterator() {
  static bool com_initialized = false;
  if (!com_initialized) {
    const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (result == S_OK || result == S_FALSE || result == RPC_E_CHANGED_MODE) {
      com_initialized = true;
    } else {
      return nullptr;
    }
  }

  IDeckLinkIterator *iterator = nullptr;
  if (CoCreateInstance(CLSID_CDeckLinkIterator, nullptr, CLSCTX_ALL,
                       IID_IDeckLinkIterator, (void **)&iterator) != S_OK) {
    return nullptr;
  }
  return iterator;
}
#elif defined(__APPLE__)
using String = CFStringRef;
using Bool = bool;

inline ::godot::String string_to_godot(String p_string) {
  if (!p_string) {
    return ::godot::String();
  }

  const CFIndex length = CFStringGetLength(p_string);
  const CFIndex max_size =
      CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  if (max_size <= 0) {
    return ::godot::String();
  }

  std::vector<char> buffer((size_t)max_size);
  if (!CFStringGetCString(p_string, buffer.data(), max_size,
                          kCFStringEncodingUTF8)) {
    return ::godot::String();
  }
  return ::godot::String::utf8(buffer.data());
}

inline void release_string(String p_string) {
  if (p_string) {
    CFRelease(p_string);
  }
}

inline IDeckLinkIterator *create_iterator() {
  return CreateDeckLinkIteratorInstance();
}
#else
using String = const char *;
using Bool = bool;

inline ::godot::String string_to_godot(String p_string) {
  return p_string ? ::godot::String::utf8(p_string) : ::godot::String();
}

inline void release_string(const char *p_string) {
  if (p_string) {
    free((void *)p_string);
  }
}

inline IDeckLinkIterator *create_iterator() {
  return CreateDeckLinkIteratorInstance();
}
#endif

template <typename T> inline void safe_release(T *&p_object) {
  if (p_object) {
    p_object->Release();
    p_object = nullptr;
  }
}

inline bool iid_equal(REFIID p_a, REFIID p_b) {
  return memcmp(&p_a, &p_b, sizeof(p_a)) == 0;
}

inline REFIID iid_unknown() {
#if defined(__APPLE__)
  return IUnknownUUID;
#else
  return IID_IUnknown;
#endif
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
