// Reading and changing the game's memory and code: object fields, code patches and hooks.
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "log.h"

namespace rhw {

// Returns a reference to a field of type T inside a game object, e.g. Field<float>(world, OFF_ZOOM).
// Writing through the reference changes the value in the game.
template <typename T>
T& Field(uintptr_t object, uint32_t offset)
{
    return *reinterpret_cast<T*>(object + offset);
}

// Value of a global pointer in the game, e.g. Global(ADDR_WORLD_PTR); 0 when it is not set.
inline uintptr_t Global(uintptr_t address)
{
    return *reinterpret_cast<uintptr_t*>(address);
}

// Bit pattern of a float as it is stored in an instruction, e.g. Bits(1024.0f) == 0x44800000.
constexpr uint32_t Bits(float value)
{
    return std::bit_cast<uint32_t>(value);
}

// Overwrites 4 bytes of game code with value, but only if they currently hold expected.
// Returns false (and changes nothing) for a different Game.exe version.
bool PatchDword(uintptr_t address, uint32_t expected, uint32_t value);

// Replaces the first size bytes of target with "JMP hook" + NOPs and returns a trampoline that executes
// the original bytes and jumps back to target + size. The bytes must be whole, position independent
// instructions. Returns nullptr if the bytes differ from the expected prologue (different game
// version) - in that case nothing is changed.
void* InstallHook(uintptr_t target, const uint8_t* expected, size_t size, void* hook);

// Installs one hook, logs the result and returns the trampoline as the original function's type.
template <typename Fn, size_t N>
Fn Hook(const char* name, uintptr_t target, const uint8_t (&expected)[N], Fn hook)
{
    static_assert(N >= 5, "a JMP rel32 needs 5 bytes");
    void* tramp = InstallHook(target, expected, N, reinterpret_cast<void*>(hook));
    char msg[96];
    snprintf(msg, sizeof(msg), tramp ? "Hook %s installed" : "Hook %s NOT installed (unknown Game.exe version)", name);
    Log(msg);
    return reinterpret_cast<Fn>(tramp);
}

}  // namespace rhw
