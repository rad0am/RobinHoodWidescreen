// RobinHoodWidescreen - fixes zoom at high resolutions in
// "Robin Hood: The Legend of Sherwood" (GOG, Game.exe 1.1.0.0).
//
// The game ships as a dinput.dll proxy: Windows loads this DLL instead of the
// system one, we install our hooks, and forward all DirectInput calls to the
// real system DLL.
//
// Problem being fixed: Camera_SetPositionClamped assumes zoom 1.0 always fits
// on the map. At 3440x1440 the view at zoom 1.0 is wider than the map, so the
// map wraps into misaligned bands and zoom 0.5 can crash the game.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ===== Addresses in Game.exe (GOG, version 1.1.0.0) =====
constexpr uintptr_t ADDR_SET_CAMERA    = 0x005758D0;   // Camera_SetPositionClamped (cdecl)
constexpr uintptr_t ADDR_CAMERA_UPDATE = 0x004C8380;   // Camera_Update (this in ECX), runs every frame
constexpr uintptr_t ADDR_WORLD_PTR     = 0x006C0FA8;   // pointer to the world object

// ===== Fields of the world object =====
constexpr uint32_t OFF_SCREEN_W = 0x4728;   // float, screen width in pixels
constexpr uint32_t OFF_SCREEN_H = 0x472C;   // float, screen height in pixels
constexpr uint32_t OFF_MAP_W    = 0x4730;   // float, map width in pixels
constexpr uint32_t OFF_MAP_H    = 0x4734;   // float, map height in pixels
constexpr uint32_t OFF_ZOOM     = 0x475C;   // float, 0.5 / 1.0 / 2.0
constexpr float    HUD_HEIGHT   = 80.0f;    // bottom interface bar, the map is not drawn under it

// Both functions start with the standard MSVC SEH prologue:
//   6A FF            PUSH -1
//   68 xx xx xx xx   PUSH <exception handler>
// These 7 bytes are position independent, so they can be copied to a trampoline as-is.
constexpr size_t PROLOGUE_SIZE = 7;
const uint8_t PROLOGUE_SET_CAMERA[PROLOGUE_SIZE]    = { 0x6A, 0xFF, 0x68, 0x1F, 0xB3, 0x66, 0x00 };
const uint8_t PROLOGUE_CAMERA_UPDATE[PROLOGUE_SIZE] = { 0x6A, 0xFF, 0x68, 0x72, 0xC6, 0x65, 0x00 };

// Appends a line to RobinHoodWidescreen.log in the game folder.
void Log(const char* text)
{
    FILE* f = nullptr;
    if (fopen_s(&f, "RobinHoodWidescreen.log", "a") == 0 && f) {
        fprintf(f, "%s\n", text);
        fclose(f);
    }
}

// Returns a reference to a field of type T inside a game object, e.g. Field<float>(world, OFF_ZOOM).
// Writing through the reference changes the value in the game.
template <typename T>
T& Field(uintptr_t object, uint32_t offset)
{
    return *reinterpret_cast<T*>(object + offset);
}

// Raises the zoom to the smallest allowed level at which the whole view fits on the map.
void EnsureZoomFits(const char* caller)
{
    uintptr_t world = *reinterpret_cast<uintptr_t*>(ADDR_WORLD_PTR);
    if (world == 0) return;                              // no mission loaded

    float screenW = Field<float>(world, OFF_SCREEN_W);
    float screenH = Field<float>(world, OFF_SCREEN_H);
    float mapW    = Field<float>(world, OFF_MAP_W);
    float mapH    = Field<float>(world, OFF_MAP_H);
    float& zoom   = Field<float>(world, OFF_ZOOM);

    if (mapW <= 0.0f || mapH <= 0.0f) return;            // map not loaded yet

    // Visible part of the map = screen / zoom, so the view fits when zoom >= screen / map.
    float needX  = screenW / mapW;
    float needY  = (screenH - HUD_HEIGHT) / mapH;
    float needed = needX > needY ? needX : needY;

    if (zoom >= needed) return;                          // current zoom already fits

    // Zooming in only shrinks the view, so the camera position stays inside the map.
    float oldZoom = zoom;
    zoom = 2.0f;                                         // fallback: the largest level the game supports
    const float levels[] = { 0.5f, 1.0f, 2.0f };
    for (float level : levels) {
        if (level >= needed) { zoom = level; break; }    // the smallest level that fits
    }

    char msg[160];
    snprintf(msg, sizeof(msg), "[%s] zoom %.2f -> %.2f (needed >= %.3f, screen %.0fx%.0f, map %.0fx%.0f)",
             caller, oldZoom, zoom, needed, screenW, screenH, mapW, mapH);
    Log(msg);
}

// ===== Hooks =====

using SetCameraFn    = void* (__cdecl*)(void* out, const float* center);
using CameraUpdateFn = int (__fastcall*)(void* self, void* edx);

SetCameraFn    g_origSetCamera    = nullptr;   // trampolines that run the original functions
CameraUpdateFn g_origCameraUpdate = nullptr;

// Called by the game whenever the camera is moved; center is the requested camera center.
void* __cdecl Hook_SetCamera(void* out, const float* center)
{
    EnsureZoomFits("SetCamera");
    return g_origSetCamera(out, center);
}

// Called by the game every frame. Catches a resolution change made while the camera is not moving.
// The original is __thiscall (this in ECX, no stack arguments); __fastcall with an unused EDX
// parameter has the same layout and can be written in plain C++.
int __fastcall Hook_CameraUpdate(void* self, void* edx)
{
    EnsureZoomFits("CameraUpdate");
    return g_origCameraUpdate(self, edx);
}

// Replaces the first 7 bytes of target with "JMP hook; NOP; NOP" and returns a trampoline that
// executes the original 7 bytes and jumps back to target + 7. Returns nullptr if the bytes differ
// from the expected prologue (different game version) - in that case nothing is changed.
void* InstallHook(uintptr_t targetAddr, const uint8_t (&expected)[PROLOGUE_SIZE], void* hook)
{
    auto* target = reinterpret_cast<uint8_t*>(targetAddr);
    if (std::memcmp(target, expected, PROLOGUE_SIZE) != 0)
        return nullptr;

    // Trampoline: original 7 bytes + JMP rel32 back to target + 7.
    // VirtualAlloc gives executable memory (memory from new is not executable because of DEP).
    auto* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return nullptr;
    std::memcpy(tramp, target, PROLOGUE_SIZE);
    tramp[7] = 0xE9;                                                       // JMP rel32
    *reinterpret_cast<int32_t*>(tramp + 8) =
        static_cast<int32_t>((target + PROLOGUE_SIZE) - (tramp + 12));     // destination - next instruction

    // Game code is read/execute only: unlock it, write the jump, restore the protection.
    DWORD oldProtect;
    VirtualProtect(target, PROLOGUE_SIZE, PAGE_EXECUTE_READWRITE, &oldProtect);
    target[0] = 0xE9;                                                      // JMP rel32
    *reinterpret_cast<int32_t*>(target + 1) =
        static_cast<int32_t>(static_cast<uint8_t*>(hook) - (target + 5));
    target[5] = 0x90;                                                      // NOP
    target[6] = 0x90;                                                      // NOP
    VirtualProtect(target, PROLOGUE_SIZE, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, PROLOGUE_SIZE);
    return tramp;
}

void InstallAllHooks()
{
    g_origSetCamera = reinterpret_cast<SetCameraFn>(
        InstallHook(ADDR_SET_CAMERA, PROLOGUE_SET_CAMERA, reinterpret_cast<void*>(&Hook_SetCamera)));
    g_origCameraUpdate = reinterpret_cast<CameraUpdateFn>(
        InstallHook(ADDR_CAMERA_UPDATE, PROLOGUE_CAMERA_UPDATE, reinterpret_cast<void*>(&Hook_CameraUpdate)));

    Log(g_origSetCamera    ? "Hook SetCamera installed"    : "Hook SetCamera NOT installed (unknown Game.exe version)");
    Log(g_origCameraUpdate ? "Hook CameraUpdate installed" : "Hook CameraUpdate NOT installed (unknown Game.exe version)");
}

// ===== dinput.dll proxy =====

// Loads the real system dinput.dll once and returns the address of one of its functions.
// The full path is required: LoadLibrary("dinput.dll") would return this proxy again.
FARPROC GetRealProc(const char* name)
{
    static HMODULE real = nullptr;
    if (!real) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);             // System32 (redirected to SysWOW64 for 32-bit)
        wcscat_s(path, L"\\dinput.dll");
        real = LoadLibraryW(path);
        if (!real) {
            Log("ERROR: could not load the system dinput.dll");
            return nullptr;
        }
    }
    FARPROC proc = GetProcAddress(real, name);
    if (!proc) {
        char msg[128];
        snprintf(msg, sizeof(msg), "ERROR: %s not found in the system dinput.dll", name);
        Log(msg);
    }
    return proc;
}

// All three creation functions are exported (see exports.def), because the game's DirectX
// version check looks them up with GetProcAddress. Signatures must match the originals exactly:
// WINAPI is __stdcall, so the callee removes its arguments from the stack.

extern "C" HRESULT WINAPI DirectInputCreateA(HINSTANCE hinst, DWORD version, LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, LPVOID*, LPUNKNOWN);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("DirectInputCreateA"));
    return real ? real(hinst, version, out, outer) : E_FAIL;
}

extern "C" HRESULT WINAPI DirectInputCreateW(HINSTANCE hinst, DWORD version, LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, LPVOID*, LPUNKNOWN);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("DirectInputCreateW"));
    return real ? real(hinst, version, out, outer) : E_FAIL;
}

extern "C" HRESULT WINAPI DirectInputCreateEx(HINSTANCE hinst, DWORD version, REFIID riid,
                                              LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    static Fn real = reinterpret_cast<Fn>(GetRealProc("DirectInputCreateEx"));
    return real ? real(hinst, version, riid, out, outer) : E_FAIL;
}

// ===== Entry point =====

// Runs when Windows loads the DLL, before any game code. Only light work is allowed here
// (the loader lock is held), so the real dinput.dll is loaded later, on first use.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Log("RobinHoodWidescreen loaded");
        InstallAllHooks();
    }
    return TRUE;
}
