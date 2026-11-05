#include "patch.h"

#include <windows.h>
#include <cstring>

namespace rhw {

bool PatchDword(uintptr_t address, uint32_t expected, uint32_t value)
{
    auto* target = reinterpret_cast<uint8_t*>(address);
    if (std::memcmp(target, &expected, sizeof(expected)) != 0)
        return false;

    DWORD oldProtect;
    VirtualProtect(target, sizeof(value), PAGE_EXECUTE_READWRITE, &oldProtect);
    std::memcpy(target, &value, sizeof(value));
    VirtualProtect(target, sizeof(value), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(value));
    return true;
}

void* InstallHook(uintptr_t targetAddr, const uint8_t* expected, size_t size, void* hook)
{
    auto* target = reinterpret_cast<uint8_t*>(targetAddr);
    if (std::memcmp(target, expected, size) != 0)
        return nullptr;

    // Trampoline: original bytes + JMP rel32 back to target + size.
    // VirtualAlloc gives executable memory (memory from new is not executable because of DEP).
    auto* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, size + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return nullptr;
    std::memcpy(tramp, target, size);
    tramp[size] = 0xE9;                                                    // JMP rel32
    *reinterpret_cast<int32_t*>(tramp + size + 1) =
        static_cast<int32_t>((target + size) - (tramp + size + 5));        // destination - next instruction

    // Game code is read/execute only: unlock it, write the jump, restore the protection.
    DWORD oldProtect;
    VirtualProtect(target, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    target[0] = 0xE9;                                                      // JMP rel32
    *reinterpret_cast<int32_t*>(target + 1) =
        static_cast<int32_t>(static_cast<uint8_t*>(hook) - (target + 5));
    std::memset(target + 5, 0x90, size - 5);                               // NOPs
    VirtualProtect(target, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, size);
    return tramp;
}

}  // namespace rhw
