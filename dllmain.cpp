// RobinHoodWidescreen - native desktop resolution and a zoom fix for high resolutions in
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
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace {

// ===== Addresses in Game.exe (GOG, version 1.1.0.0) =====
constexpr uintptr_t ADDR_SET_CAMERA    = 0x005758D0;   // Camera_SetPositionClamped (cdecl)
constexpr uintptr_t ADDR_CAMERA_UPDATE = 0x004C8380;   // Camera_Update (this in ECX), runs every frame
constexpr uintptr_t ADDR_WORLD_PTR     = 0x006C0FA8;   // pointer to the world object

// ===== Resolution constants in Game.exe (operands of PUSH / CMP instructions) =====
// Options menu (FUN_0052CF80): each of the three buttons does "push <height>f; push <width>f".
// Radio button selection (FUN_0052D090): "cmp eax, <width>" per button. Any other width logs
// "Warning, unknown screen resolution." and leaves no button selected.
struct MenuButton {
    const char* label;          // text shown in the menu (comes from Level.res, not patched)
    uint32_t    width, height;  // original resolution
    uintptr_t   addrHeight;     // immediate of push <height>f
    uintptr_t   addrWidth;      // immediate of push <width>f
    uintptr_t   addrSelect;     // immediate of cmp eax, <width>
};
constexpr MenuButton MENU_BUTTONS[] = {
    { "640x480",   640, 480, 0x0052CFAA, 0x0052CFAF, 0x0052D0A2 },
    { "800x600",   800, 600, 0x0052CFF3, 0x0052CFF8, 0x0052D0B9 },
    { "1024x768", 1024, 768, 0x0052D039, 0x0052D03E, 0x0052D0C0 },
};
// Settings init (FUN_0055D670), default for a new profile: push 600.0f; push 800.0f
constexpr uintptr_t ADDR_DEFAULT_HEIGHT = 0x0055D763;
constexpr uintptr_t ADDR_DEFAULT_WIDTH  = 0x0055D768;

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

constexpr const char* MOD_VERSION = "0.3";

// Appends a line to RobinHoodWidescreen.log in the game folder. The first call of each run
// truncates the file, so the log only holds the current session.
void Log(const char* text)
{
    static bool firstCall = true;
    FILE* f = nullptr;
    if (fopen_s(&f, "RobinHoodWidescreen.log", firstCall ? "w" : "a") == 0 && f) {
        firstCall = false;
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
    auto world = *reinterpret_cast<uintptr_t*>(ADDR_WORLD_PTR);
    if (world == 0) return;                              // no mission loaded

    float screenW = Field<float>(world, OFF_SCREEN_W);
    float screenH = Field<float>(world, OFF_SCREEN_H);
    float mapW    = Field<float>(world, OFF_MAP_W);
    float mapH    = Field<float>(world, OFF_MAP_H);
    auto& zoom    = Field<float>(world, OFF_ZOOM);

    if (mapW <= 0.0f || mapH <= 0.0f) return;            // map not loaded yet

    // Visible part of the map = screen / zoom, so the view fits when zoom >= screen / map.
    float needX  = screenW / mapW;
    float needY  = (screenH - HUD_HEIGHT) / mapH;
    float needed = needX > needY ? needX : needY;

    if (zoom >= needed) return;                          // current zoom already fits

    // Zooming in only shrinks the view, so the camera position stays inside the map.
    float oldZoom = zoom;
    zoom = 2.0f;                                         // fallback: the largest level the game supports
    constexpr float levels[] = { 0.5f, 1.0f, 2.0f };
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

// ===== Resolution =====

// Bit pattern of a float as it is stored in an instruction, e.g. Bits(1024.0f) == 0x44800000.
constexpr uint32_t Bits(float value)
{
    return std::bit_cast<uint32_t>(value);
}

// Overwrites 4 bytes of game code with value, but only if they currently hold expected.
// Returns false (and changes nothing) for a different Game.exe version.
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

// Physical resolution of the primary monitor. DirectDraw and the GOG wrapper use the primary
// monitor. GetSystemMetrics(SM_CXSCREEN) is not used: the game is not DPI aware, so with
// Windows scaling at 125%/150% it would return scaled-down values.
bool GetDesktopResolution(DWORD& width, DWORD& height)
{
    HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return false;

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        return false;

    width  = mode.dmPelsWidth;
    height = mode.dmPelsHeight;
    return true;
}

// Render size with the desktop's aspect ratio for the given height. The width is rounded to a
// multiple of 8 (old DirectDraw code and the GOG wrapper are happier with aligned surfaces);
// the aspect ratio error is far below what is visible.
uint32_t WidthForHeight(DWORD desktopW, DWORD desktopH, uint32_t height)
{
    double width = static_cast<double>(desktopW) * height / desktopH;
    return static_cast<uint32_t>(width / 8.0 + 0.5) * 8;
}

// Points one menu button at a new resolution: both push immediates and the selection compare.
bool PatchMenuButton(const MenuButton& button, uint32_t width, uint32_t height)
{
    // The compare is checked first, so a different Game.exe version fails before anything is written.
    if (std::memcmp(reinterpret_cast<void*>(button.addrSelect), &button.width, sizeof(uint32_t)) != 0)
        return false;
    return PatchDword(button.addrHeight, Bits(static_cast<float>(button.height)), Bits(static_cast<float>(height)))
        && PatchDword(button.addrWidth,  Bits(static_cast<float>(button.width)),  Bits(static_cast<float>(width)))
        && PatchDword(button.addrSelect, button.width, width);
}

// Replaces the three menu resolutions with sizes that have the desktop's aspect ratio, and the
// new-profile default (800x600) with the native size. Game.exe on disk stays unmodified.
//   "640x480"  -> height 768: the interface (designed for 1024x768) keeps its original on-screen size
//   "800x600"  -> height 1080 (or halfway between 768 and native on smaller desktops)
//   "1024x768" -> native desktop resolution: the most map, the smallest interface
// The GOG wrapper (display=desktop, scaling=fit) keeps the monitor in its native mode and scales
// the smaller sizes up, so they do not have to be real display modes.
void PatchResolution()
{
    DWORD desktopW = 0, desktopH = 0;
    if (!GetDesktopResolution(desktopW, desktopH)) {
        Log("Resolution: could not read the desktop resolution, menu left unchanged");
        return;
    }
    if (desktopW <= 1024 || desktopH < 768) {
        Log("Resolution: desktop is not larger than 1024x768, menu left unchanged");
        return;
    }

    uint32_t middleH = desktopH > 1080 ? 1080 : (desktopH + 768) / 2 / 2 * 2;
    struct Size { uint32_t width, height; };
    const Size sizes[] = {
        { WidthForHeight(desktopW, desktopH, 768), 768 },
        { WidthForHeight(desktopW, desktopH, middleH), middleH },
        { desktopW, desktopH },
    };

    char msg[400];
    int len = snprintf(msg, sizeof(msg), "Resolution: desktop %lux%lu;", desktopW, desktopH);
    uint32_t usedWidths[3] = {};
    for (size_t i = 0; i < std::size(MENU_BUTTONS); ++i) {
        const MenuButton& button = MENU_BUTTONS[i];
        Size size = sizes[i];

        // The selection compares widths only, so every button needs its own width. A size smaller
        // than the interface (1024x768) or not smaller than native (except the native button) is skipped.
        bool isNative  = i == std::size(MENU_BUTTONS) - 1;
        bool tooSmall  = size.width < 1024 || size.height < 768;
        bool notSmaller = !isNative && size.height >= desktopH;
        bool duplicate = std::find(std::begin(usedWidths), std::end(usedWidths), size.width) != std::end(usedWidths);

        const char* result;
        if (tooSmall || notSmaller || duplicate) {
            result = "skipped";
        } else if (PatchMenuButton(button, size.width, size.height)) {
            usedWidths[i] = size.width;
            result = "patched";
        } else {
            result = "NOT patched";
        }
        len += snprintf(msg + len, sizeof(msg) - len, " %s -> %ux%u %s,", button.label, size.width, size.height, result);
    }

    uint32_t w = Bits(static_cast<float>(desktopW));
    uint32_t h = Bits(static_cast<float>(desktopH));
    bool defaults = PatchDword(ADDR_DEFAULT_HEIGHT, Bits(600.0f), h) && PatchDword(ADDR_DEFAULT_WIDTH, Bits(800.0f), w);
    snprintf(msg + len, sizeof(msg) - len, " new profile default %s", defaults ? "patched" : "NOT patched");
    Log(msg);
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

}  // namespace

// All three creation functions are exported (see exports.def), because the game's DirectX
// version check looks them up with GetProcAddress. Signatures must match the originals exactly:
// WINAPI is __stdcall, so the callee removes its arguments from the stack.

extern "C" HRESULT WINAPI DirectInputCreateA(HINSTANCE hinst, DWORD version, LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, LPVOID*, LPUNKNOWN);
    static auto real = reinterpret_cast<Fn>(GetRealProc("DirectInputCreateA"));
    return real ? real(hinst, version, out, outer) : E_FAIL;
}

extern "C" HRESULT WINAPI DirectInputCreateW(HINSTANCE hinst, DWORD version, LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, LPVOID*, LPUNKNOWN);
    static auto real = reinterpret_cast<Fn>(GetRealProc("DirectInputCreateW"));
    return real ? real(hinst, version, out, outer) : E_FAIL;
}

extern "C" HRESULT WINAPI DirectInputCreateEx(HINSTANCE hinst, DWORD version, REFIID riid,
                                              LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    static auto real = reinterpret_cast<Fn>(GetRealProc("DirectInputCreateEx"));
    return real ? real(hinst, version, riid, out, outer) : E_FAIL;
}

// ===== Entry point =====

// Runs when Windows loads the DLL, before any game code. Only light work is allowed here
// (the loader lock is held), so the real dinput.dll is loaded later, on first use.
// user32 is safe to call: we import it, so Windows initializes it before our DllMain.
// extern "C": the CRT startup code calls DllMain by its undecorated C name.
extern "C" BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        char msg[64];
        snprintf(msg, sizeof(msg), "RobinHoodWidescreen %s loaded", MOD_VERSION);
        Log(msg);
        PatchResolution();
        InstallAllHooks();
    }
    return TRUE;
}
