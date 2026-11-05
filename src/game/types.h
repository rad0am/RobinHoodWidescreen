// Game data structures the mod reads or writes. Their layout must match Game.exe byte for byte.
#pragma once

#include <cstdint>

namespace rhw {

// Rectangle as passed to SBDrawManager::Blit (floats, converted to a RECT with __ftol inside).
// The mod only passes it on; [[maybe_unused]] marks fields that exist for the layout.
struct FloatRect {
    [[maybe_unused]] float left, top, right, bottom;
};

// std::wstring as compiled into Game.exe (MSVC 6 STL, 16 bytes). The buffer is reference counted:
// the byte just before ptr holds the count and copies share it, so the text must never be written
// in place. _Grow gives the string its own buffer, allocated by the game's heap (which also frees it).
struct GameWString {
    [[maybe_unused]] uint8_t  allocator;   // empty allocator object
    wchar_t*                  ptr;         // text, 0-terminated; nullptr when empty
    uint32_t                  length;      // in characters
    [[maybe_unused]] uint32_t capacity;    // in characters
};
static_assert(sizeof(GameWString) == 16, "must match the game's std::wstring");

}  // namespace rhw
