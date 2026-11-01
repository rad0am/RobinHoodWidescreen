// RobinHoodWidescreen - native desktop resolution for "Robin Hood: The Legend of Sherwood"
// (GOG, Game.exe 1.1.0.0).
//
// The game ships as a dinput.dll proxy: Windows loads this DLL instead of the
// system one, we install our hooks, and forward all DirectInput calls to the
// real system DLL.
//
// What it does:
//   - the resolution menu offers sizes with the desktop's aspect ratio, native included;
//   - zoom fix: Camera_SetPositionClamped assumes zoom 1.0 always fits on the map. At 3440x1440
//     the view at zoom 1.0 is wider than the map, so the map wraps into misaligned bands and
//     zoom 0.5 can crash the game;
//   - dialogs (briefing, pause menu, options, ...) keep the size they have at height 768.

#include <windows.h>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <iterator>

namespace {

// ===== Addresses in Game.exe (GOG, version 1.1.0.0) =====
constexpr uintptr_t ADDR_SET_CAMERA    = 0x005758D0;   // Camera_SetPositionClamped (cdecl)
constexpr uintptr_t ADDR_CAMERA_UPDATE = 0x004C8380;   // Camera_Update (this in ECX), runs every frame
constexpr uintptr_t ADDR_WORLD_PTR     = 0x006C0FA8;   // pointer to the world object
constexpr uintptr_t ADDR_GET_TEXT      = 0x006009E0;   // Resource_GetText (this in ECX): string <index> of TEXT <id>
constexpr uintptr_t ADDR_WSTRING_GROW  = 0x004239E0;   // std::wstring::_Grow(n, trim) of the game's CRT (this in ECX)
constexpr uintptr_t ADDR_BLIT          = 0x005E6440;   // SBDrawManager::Blit (this in ECX): IDirectDrawSurface7::Blt wrapper
constexpr uintptr_t ADDR_SURFACE_W     = 0x005E56C0;   // SBDrawManager::GetSurfaceWidth(id) (this in ECX), id 0 = back buffer
constexpr uintptr_t ADDR_SURFACE_H     = 0x005E5750;   // SBDrawManager::GetSurfaceHeight(id)
constexpr uintptr_t ADDR_CREATE_SURFACE = 0x005E4ED0;  // SBDrawManager::CreateSurface(w, h, ?, keep) -> id (this in ECX)
constexpr uintptr_t ADDR_PRESENT       = 0x005E5260;   // SBDrawManager::Present() (this in ECX): Flip
constexpr uintptr_t ADDR_SURFACE_FIND  = 0x005F0090;   // surface map lower_bound(const int* id) (this = map in ECX)
constexpr uintptr_t ADDR_RUN_MODAL     = 0x0053C390;   // modal dialog loop (this in ECX), returns when the dialog closes
constexpr uintptr_t ADDR_MOUSE_MOVE    = 0x005F4950;   // mouse moved (cdecl): (const float* newPosition), absolute screen pixels
constexpr uintptr_t ADDR_MOUSE_POS     = 0x006C1300;   // float x, y: mouse position used by the cursor and the interface
constexpr uintptr_t ADDR_DRAW_MANAGER  = 0x006C11D0;   // pointer to SBDrawManager

// ===== Fields of SBDrawManager =====
constexpr uint32_t OFF_DM_SURFACES = 0x54;    // std::map<int, Surface>: surface id -> DirectDraw surface
constexpr uint32_t OFF_DM_WIDTH    = 0x10E;   // u16, current screen width
constexpr uint32_t OFF_DM_HEIGHT   = 0x110;   // u16, current screen height

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
// The button labels are strings 43/44/45 of the interface text table (TEXT 1000507 in Level.res).
constexpr uint32_t UI_TEXT_ID           = 1000507;
constexpr uint32_t UI_TEXT_FIRST_BUTTON = 43;
// New labels ("1832x768", ...) for the patched buttons; empty = keep the game's text.
wchar_t g_buttonLabels[std::size(MENU_BUTTONS)][16] = {};
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

// ===== Function prologues replaced by the hooks =====
// The first bytes of each hooked function are copied to a trampoline, so they must be whole
// instructions that do not depend on where they are (no relative jumps or calls). They are also
// compared before patching, so a different Game.exe version is left untouched.
//
// Standard MSVC SEH prologue, 7 bytes: PUSH -1; PUSH <exception handler>
const uint8_t PROLOGUE_SET_CAMERA[7]    = { 0x6A, 0xFF, 0x68, 0x1F, 0xB3, 0x66, 0x00 };
const uint8_t PROLOGUE_CAMERA_UPDATE[7] = { 0x6A, 0xFF, 0x68, 0x72, 0xC6, 0x65, 0x00 };
const uint8_t PROLOGUE_BLIT[7]          = { 0x6A, 0xFF, 0x68, 0xC8, 0x28, 0x67, 0x00 };
// Present reads the SEH list first, 8 bytes: MOV EAX, FS:[0]; PUSH -1
const uint8_t PROLOGUE_FS_SEH[8]        = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xFF };
// Resource_GetText, 7 bytes: SUB ESP, 0x10; LEA EAX, [ESP+0x14]
const uint8_t PROLOGUE_GET_TEXT[7]      = { 0x83, 0xEC, 0x10, 0x8D, 0x44, 0x24, 0x14 };
// GetSurfaceWidth/Height, 7 bytes: MOV EAX, [ESP+4]; SUB ESP, 8
const uint8_t PROLOGUE_SURFACE_SIZE[7]  = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x08 };
// Surface map lookup, 8 bytes: PUSH ECX; PUSH ESI; MOV ESI, ECX; LEA ECX, [ESP+4]
const uint8_t PROLOGUE_SURFACE_FIND[8]  = { 0x51, 0x56, 0x8B, 0xF1, 0x8D, 0x4C, 0x24, 0x04 };
// RunModal, 8 bytes: SUB ESP, 0x10; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI, ECX
const uint8_t PROLOGUE_RUN_MODAL[8]     = { 0x83, 0xEC, 0x10, 0x53, 0x55, 0x56, 0x8B, 0xF1 };
// Mouse move, 8 bytes: SUB ESP, 0x14; MOV ECX, 0x6C12E8 (an absolute address, fine to copy)
const uint8_t PROLOGUE_MOUSE_MOVE[8]    = { 0x83, 0xEC, 0x14, 0xB9, 0xE8, 0x12, 0x6C, 0x00 };

// Rectangle as passed to SBDrawManager::Blit (floats, converted to a RECT with __ftol inside).
struct FloatRect {
    float left, top, right, bottom;
};

// std::wstring as compiled into Game.exe (MSVC 6 STL, 16 bytes). The buffer is reference counted:
// the byte just before ptr holds the count and copies share it, so the text must never be written
// in place. _Grow gives the string its own buffer, allocated by the game's heap (which also frees it).
struct GameWString {
    uint8_t  allocator;   // empty allocator object
    wchar_t* ptr;         // text, 0-terminated; nullptr when empty
    uint32_t length;      // in characters
    uint32_t capacity;    // in characters
};
static_assert(sizeof(GameWString) == 16, "must match the game's std::wstring");

constexpr const char* MOD_VERSION = "0.5";

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

using SetCameraFn     = void* (__cdecl*)(void* out, const float* center);
using ThiscallNoArgFn = int (__fastcall*)(void* self, void* edx);    // __thiscall without stack arguments
using GetTextFn       = int (__fastcall*)(void* self, void* edx, uint32_t id, uint32_t index, GameWString* out);
using WStringGrowFn   = bool (__fastcall*)(GameWString* self, void* edx, uint32_t length, bool trim);
using BlitFn          = int (__fastcall*)(void* self, void* edx, int srcId, const FloatRect* srcRect,
                                          int dstId, const FloatRect* dstRect, uint32_t flags);
using SurfaceSizeFn   = uint32_t (__fastcall*)(void* self, void* edx, int surfaceId);   // u16 in AX
using SurfaceFindFn   = void* (__fastcall*)(void* map, void* edx, const int* key);
using MouseMoveFn     = void (__cdecl*)(const float* position);
using CreateSurfaceFn = int (__fastcall*)(void* self, void* edx, uint32_t width, uint32_t height,
                                          uint32_t unknown, uint32_t keep);

SetCameraFn     g_origSetCamera     = nullptr;   // trampolines that run the original functions
ThiscallNoArgFn g_origCameraUpdate  = nullptr;
GetTextFn       g_origGetText       = nullptr;
BlitFn          g_gameBlit          = nullptr;   // the game's Blit, called directly (not hooked)
SurfaceSizeFn   g_origSurfaceW      = nullptr;
SurfaceSizeFn   g_origSurfaceH      = nullptr;
SurfaceFindFn   g_origSurfaceLookup = nullptr;
ThiscallNoArgFn g_origPresent       = nullptr;
ThiscallNoArgFn g_origRunModal      = nullptr;
MouseMoveFn     g_origMouseMove     = nullptr;

// Called by the game whenever the camera is moved; center is the requested camera center.
void* __cdecl Hook_SetCamera(void* out, const float* center)
{
    EnsureZoomFits("SetCamera");
    return g_origSetCamera(out, center);
}

// ===== Interface scaling for dialogs =====
//
// The interface was designed for 1024x768. At a higher resolution the dialogs (briefing scroll, pause
// menu, options, ...) are laid out and drawn for a virtual screen of height 768 with the monitor's
// aspect ratio (3440x1440 -> 1832x768, 1920x1080 -> 1368x768) and stretched over the real screen.
// The map keeps the full resolution.
//
// How a dialog is shown by the game: the window layer creates a dialog surface of the screen size
// (0x53B6B0: CreateSurface(GetSurfaceWidth(0), GetSurfaceHeight(0))), copies the screen into it
// (the "snapshot", a Blit returning to 0x53B706), draws the dialog, copies it to the back buffer and
// runs a modal loop (RunModal) until the player closes it. Some dialogs (the mission summary scroll)
// skip the window layer. Text, buttons and the cursor are drawn by software blitters straight onto
// the back buffer.
//
// The dialog mode starts when a known dialog start asks for the screen size (DIALOG_START_CALLERS)
// and ends when the modal loop returns. While it is on:
//   - the surface map lookup returns a virtual screen page instead of the back buffer (id 1), so every
//     draw function, including the software blitters, draws into it, and the screen size the dialog
//     code asks for is the page's size;
//   - SBDrawManager's screen size (used to clip software blits) and the mouse are virtual;
//   - Present stretches the virtual page over the real back buffer before the flip.

constexpr uint32_t  UI_HEIGHT              = 768;        // the interface's design height (1024x768)
constexpr int       BACK_BUFFER_ID         = 1;          // surface id 0 is replaced by 1 before every lookup

bool     g_dialogHooksOk   = false;   // all hooks needed for dialog scaling are installed
bool     g_dialogActive    = false;   // from the dialog start until the modal loop returns
int      g_virtualPages[2] = {};      // two virtual screen pages, swapped on every Present like a flip chain
int      g_virtualPage     = 0;       // index of the page currently used as the back buffer
uint32_t g_virtualW = 0, g_virtualH = 0;
bool     g_bypassRedirect  = false;   // true while our own code (or Present) needs the real back buffer
uint16_t g_savedDmW = 0, g_savedDmH = 0;   // real screen size while the draw manager holds the virtual one
int      g_modalDepth      = 0;       // nesting of RunModal (a dialog opened from a dialog)
int      g_cameraDepth     = 0;       // > 0 while Camera_Update (a world frame) runs
int      g_worldFramesInDialog = 0;   // world frames since the dialog opened, outside a modal loop
constexpr int WORLD_FRAMES_TO_END_DIALOG = 30;

// Real back buffer size and the virtual interface size (same aspect, height 768).
struct ScreenSizes {
    uint32_t realW, realH, virtualW, virtualH;
    float    scale;   // real / virtual, 1.0 when there is nothing to scale
};

ScreenSizes GetScreenSizes()
{
    ScreenSizes s{ 0, 0, 0, 0, 1.0f };
    auto dm = *reinterpret_cast<uintptr_t*>(ADDR_DRAW_MANAGER);
    if (!dm) return s;
    s.realW = g_savedDmW ? g_savedDmW : Field<uint16_t>(dm, OFF_DM_WIDTH);
    s.realH = g_savedDmH ? g_savedDmH : Field<uint16_t>(dm, OFF_DM_HEIGHT);
    if (s.realH <= UI_HEIGHT) { s.virtualW = s.realW; s.virtualH = s.realH; return s; }
    s.virtualH = UI_HEIGHT;
    s.virtualW = static_cast<uint32_t>(s.realW * static_cast<double>(UI_HEIGHT) / s.realH / 8.0 + 0.5) * 8;
    s.scale = static_cast<float>(s.realH) / UI_HEIGHT;
    return s;
}

// Dialogs are scaled only in a mission and only when the screen is taller than the interface. (The
// world pointer is still set for a moment after leaving a mission, which is why only the known dialog
// starts switch the mode on, not any menu code.)
bool DialogScalingWanted()
{
    return g_dialogHooksOk && *reinterpret_cast<uintptr_t*>(ADDR_WORLD_PTR) != 0 && GetScreenSizes().scale > 1.0f;
}

// Screen size fields of SBDrawManager. The clipping helper 0x5E6000 clips every software blit against
// them, so while a dialog draws into the virtual screen they must hold its size, or blits run past the
// end of the smaller surface (a crash in the asm blitter 0x622550).
void SetDrawManagerSize(uint16_t width, uint16_t height)
{
    auto dm = *reinterpret_cast<uintptr_t*>(ADDR_DRAW_MANAGER);
    if (!dm) return;
    Field<uint16_t>(dm, OFF_DM_WIDTH)  = width;
    Field<uint16_t>(dm, OFF_DM_HEIGHT) = height;
}

// Converts the game's current mouse position between real and virtual coordinates.
void ScaleMousePosition(float factor)
{
    auto* position = reinterpret_cast<float*>(ADDR_MOUSE_POS);
    position[0] *= factor;
    position[1] *= factor;
}

// Creates the two virtual screen pages (or new ones after a resolution change) with the game's own
// CreateSurface, so the draw manager knows them like any other surface. Two pages, because the real
// screen is a flip chain: while paused, the game removes the cursor by restoring the background it
// saved on the same page two frames earlier (surfaces 993/994 alternate). With a single virtual page
// the cursor of the previous frame would never be removed.
bool EnsureVirtualPages(void* drawManager)
{
    ScreenSizes s = GetScreenSizes();
    if (g_virtualPages[0] > 0 && g_virtualW == s.virtualW && g_virtualH == s.virtualH)
        return true;
    static const auto create = reinterpret_cast<CreateSurfaceFn>(ADDR_CREATE_SURFACE);
    int ids[2] = { create(drawManager, nullptr, s.virtualW, s.virtualH, 0, 0),
                   create(drawManager, nullptr, s.virtualW, s.virtualH, 0, 0) };
    char msg[128];
    snprintf(msg, sizeof(msg), "Dialogs: virtual screen %ux%u (scale %.3f), surfaces %d and %d",
             s.virtualW, s.virtualH, s.scale, ids[0], ids[1]);
    Log(msg);
    if (ids[0] <= 0 || ids[1] <= 0) return false;
    g_virtualPages[0] = ids[0];
    g_virtualPages[1] = ids[1];
    g_virtualW = s.virtualW;
    g_virtualH = s.virtualH;
    return true;
}

// Enters the dialog mode at a dialog start: both virtual pages get a shrunk copy of the screen, then
// the draw manager size and the mouse switch to virtual coordinates.
void BeginDialogMode(void* drawManager, void* edx)
{
    if (!EnsureVirtualPages(drawManager)) return;
    g_bypassRedirect = true;
    g_gameBlit(drawManager, edx, 0, nullptr, g_virtualPages[0], nullptr, 0);   // no rects: DirectDraw stretches
    g_gameBlit(drawManager, edx, 0, nullptr, g_virtualPages[1], nullptr, 0);
    g_bypassRedirect = false;

    ScreenSizes s = GetScreenSizes();
    g_savedDmW = static_cast<uint16_t>(s.realW);
    g_savedDmH = static_cast<uint16_t>(s.realH);
    SetDrawManagerSize(static_cast<uint16_t>(s.virtualW), static_cast<uint16_t>(s.virtualH));
    ScaleMousePosition(1.0f / s.scale);
    g_virtualPage = 0;
    g_worldFramesInDialog = 0;
    g_dialogActive = true;

    char msg[96];
    snprintf(msg, sizeof(msg), "Dialog opened (modal depth %d%s)", g_modalDepth,
             g_cameraDepth ? ", during a world frame" : "");
    Log(msg);
}

// Leaves the dialog mode: the back buffer, the draw manager size and the mouse are real again.
void EndDialogMode(const char* reason)
{
    if (!g_dialogActive) return;
    ScaleMousePosition(GetScreenSizes().scale);
    g_dialogActive = false;
    SetDrawManagerSize(g_savedDmW, g_savedDmH);
    g_savedDmW = g_savedDmH = 0;

    char msg[96];
    snprintf(msg, sizeof(msg), "Dialog closed (%s)", reason);
    Log(msg);
}

// Writes the virtual page id into the caller's key variable, but only when it is on this thread's
// stack (a temporary). A key stored in the map itself or in an object must never be changed: that
// would corrupt the surface map for good. Returns false when nothing was written.
bool OverwriteKey(const int* key)
{
    auto address    = reinterpret_cast<uintptr_t>(key);
    auto stackBase  = static_cast<uintptr_t>(__readfsdword(0x04));   // NT_TIB.StackBase (top)
    auto stackLimit = static_cast<uintptr_t>(__readfsdword(0x08));   // NT_TIB.StackLimit (bottom)
    if (address < stackLimit || address + sizeof(int) > stackBase)
        return false;
    *const_cast<int*>(key) = g_virtualPages[g_virtualPage];
    return true;
}

// The surface map lookup (std::map lower_bound, __thiscall, RET 4) used by every draw function.
// While a dialog is open, the back buffer key is swapped for the current virtual page. The key is
// changed in the caller's own variable: callers such as find (0x5EFBC0) compare the node they get
// with that variable afterwards, and would treat a node with a different key as "not found".
void* __fastcall Hook_SurfaceLookup(void* map, void* edx, const int* key)
{
    if (g_dialogActive && !g_bypassRedirect && *key == BACK_BUFFER_ID) {
        auto dm = *reinterpret_cast<uintptr_t*>(ADDR_DRAW_MANAGER);
        if (reinterpret_cast<uintptr_t>(map) == dm + OFF_DM_SURFACES)
            OverwriteKey(key);
    }
    return g_origSurfaceLookup(map, edx, key);
}

// Return addresses of the screen size requests that start a dialog. Every dialog asks for the screen
// size before it is laid out and drawn; starting the dialog mode right there makes the rest of the
// dialog (layout, surfaces, drawing) virtual through the lookup redirect. Other menu code (the main
// menu, the in-game screens built at mission start) must keep the real size.
constexpr uintptr_t DIALOG_START_CALLERS[] = {
    0x53B6DF, 0x53B6E9,   // window layer 0x53B6B0: height, width of the new dialog surface (briefing, pause menu, ...)
    0x544087, 0x5440AA,   // scroll: layout (also when it opens without the window layer, e.g. the mission summary)
    0x5441C0, 0x5441CD,   // scroll: background copy surface
};

bool IsDialogStart(uintptr_t caller)
{
    return std::find(std::begin(DIALOG_START_CALLERS), std::end(DIALOG_START_CALLERS), caller)
        != std::end(DIALOG_START_CALLERS);
}

// Logs (once per address) menu code that asks for the screen size in a mission without being a known
// dialog start: if a dialog ever shows up small, its caller will be in the log.
void TraceUnknownCaller(uintptr_t caller)
{
    static uintptr_t seen[64];
    static size_t count = 0;
    if (caller < 0x520000 || caller >= 0x54A000 || g_dialogActive || !DialogScalingWanted())
        return;
    if (std::find(seen, seen + count, caller) != seen + count || count == std::size(seen))
        return;
    seen[count++] = caller;
    char msg[96];
    snprintf(msg, sizeof(msg), "Screen size asked by menu code at %08X (not a dialog start)", static_cast<unsigned>(caller));
    Log(msg);
}

// GetSurfaceWidth/Height(id), __thiscall, RET 4. A known dialog start asking for the screen size
// (id 0) switches the dialog mode on first, so the answer already comes from the virtual page.
uint32_t __fastcall Hook_SurfaceW(void* self, void* edx, int surfaceId)
{
    auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (surfaceId == 0 && !g_dialogActive) {
        if (IsDialogStart(caller) && DialogScalingWanted())
            BeginDialogMode(self, edx);
        else
            TraceUnknownCaller(caller);
    }
    return g_origSurfaceW(self, edx, surfaceId) & 0xFFFF;
}

uint32_t __fastcall Hook_SurfaceH(void* self, void* edx, int surfaceId)
{
    auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (surfaceId == 0 && !g_dialogActive) {
        if (IsDialogStart(caller) && DialogScalingWanted())
            BeginDialogMode(self, edx);
        else
            TraceUnknownCaller(caller);
    }
    return g_origSurfaceH(self, edx, surfaceId) & 0xFFFF;
}

// SBDrawManager::Present() (__thiscall, no arguments): flips the back buffer to the screen.
// With a dialog open, the virtual page is stretched over the real back buffer first.
int __fastcall Hook_Present(void* self, void* edx)
{
    if (!g_dialogActive)
        return g_origPresent(self, edx);
    // Present works on the real surfaces: give it the real size back for the duration.
    SetDrawManagerSize(g_savedDmW, g_savedDmH);
    g_bypassRedirect = true;
    g_gameBlit(self, edx, g_virtualPages[g_virtualPage], nullptr, 0, nullptr, 0);   // no rects: DirectDraw stretches
    int result = g_origPresent(self, edx);
    g_bypassRedirect = false;
    SetDrawManagerSize(static_cast<uint16_t>(g_virtualW), static_cast<uint16_t>(g_virtualH));
    g_virtualPage ^= 1;                                   // the flip: draw into the other page next
    return result;
}

// RunModal (__thiscall, no arguments): the loop of a modal dialog, returns when the player closes it.
// Ending the dialog mode right here (and not at the next world frame) keeps the clicks and drawing
// that follow the dialog in real coordinates. The depth counter handles nested dialogs.
int __fastcall Hook_RunModal(void* self, void* edx)
{
    ++g_modalDepth;
    int result = g_origRunModal(self, edx);
    if (--g_modalDepth == 0)
        EndDialogMode("modal loop ended");
    return result;
}

// Mouse moved (cdecl, one argument: pointer to the new absolute position in screen pixels).
// While a dialog is open the interface works in virtual coordinates, so the position is scaled down.
void __cdecl Hook_MouseMove(const float* position)
{
    if (!g_dialogActive)
        return g_origMouseMove(position);
    float scale = GetScreenSizes().scale;
    float scaled[2] = { position[0] / scale, position[1] / scale };
    g_origMouseMove(scaled);
}

// Called by the game every frame. Catches a resolution change made while the camera is not moving.
// The original is __thiscall (this in ECX, no stack arguments); __fastcall with an unused EDX
// parameter has the same layout and can be written in plain C++.
int __fastcall Hook_CameraUpdate(void* self, void* edx)
{
    EnsureZoomFits("CameraUpdate");

    // Fallback for a dialog that never enters RunModal. Some dialogs (the mission summary) open during
    // a world frame and the world keeps running for a moment before their modal loop starts, so the
    // mode ends only after a run of world frames without a modal loop.
    if (g_dialogActive && g_modalDepth == 0 && ++g_worldFramesInDialog >= WORLD_FRAMES_TO_END_DIALOG)
        EndDialogMode("world running without a modal loop");

    ++g_cameraDepth;
    int result = g_origCameraUpdate(self, edx);
    --g_cameraDepth;
    return result;
}

// ===== Menu labels =====

// Replaces the text of a game string, the same way the game's own GetUIText (0x5E23D0) does it.
void SetGameString(GameWString* str, const wchar_t* text)
{
    static const auto grow = reinterpret_cast<WStringGrowFn>(ADDR_WSTRING_GROW);
    auto length = static_cast<uint32_t>(wcslen(text));
    if (!grow(str, nullptr, length, true))
        return;
    wmemcpy(str->ptr, text, length);
    str->ptr[length] = L'\0';
    str->length = length;
}

// Called by the game for every text it reads from a .res file (menus, missions, dialogs).
// The original is __thiscall with 3 stack arguments (RET 0xC); __fastcall with an unused EDX
// has the same layout. Returns nonzero when the text was found.
int __fastcall Hook_GetText(void* self, void* edx, uint32_t id, uint32_t index, GameWString* out)
{
    int found = g_origGetText(self, edx, id, index, out);
    uint32_t button = index - UI_TEXT_FIRST_BUTTON;   // wraps around for indexes below 43
    if (found && id == UI_TEXT_ID && button < std::size(MENU_BUTTONS) && g_buttonLabels[button][0]) {
        SetGameString(out, g_buttonLabels[button]);

        static bool logged[std::size(MENU_BUTTONS)] = {};
        if (!logged[button]) {
            logged[button] = true;
            char msg[96];
            snprintf(msg, sizeof(msg), "[GetText] label %u replaced with %ls", index, g_buttonLabels[button]);
            Log(msg);
        }
    }
    return found;
}

// ===== Hook installation =====

// Replaces the first N bytes of target with "JMP hook" + NOPs and returns a trampoline that executes
// the original N bytes and jumps back to target + N. The N bytes must be whole, position independent
// instructions. Returns nullptr if the bytes differ from the expected prologue (different game
// version) - in that case nothing is changed.
template <size_t N>
void* InstallHook(uintptr_t targetAddr, const uint8_t (&expected)[N], void* hook)
{
    static_assert(N >= 5, "a JMP rel32 needs 5 bytes");
    auto* target = reinterpret_cast<uint8_t*>(targetAddr);
    if (std::memcmp(target, expected, N) != 0)
        return nullptr;

    // Trampoline: original N bytes + JMP rel32 back to target + N.
    // VirtualAlloc gives executable memory (memory from new is not executable because of DEP).
    auto* tramp = static_cast<uint8_t*>(
        VirtualAlloc(nullptr, N + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return nullptr;
    std::memcpy(tramp, target, N);
    tramp[N] = 0xE9;                                                       // JMP rel32
    *reinterpret_cast<int32_t*>(tramp + N + 1) =
        static_cast<int32_t>((target + N) - (tramp + N + 5));              // destination - next instruction

    // Game code is read/execute only: unlock it, write the jump, restore the protection.
    DWORD oldProtect;
    VirtualProtect(target, N, PAGE_EXECUTE_READWRITE, &oldProtect);
    target[0] = 0xE9;                                                      // JMP rel32
    *reinterpret_cast<int32_t*>(target + 1) =
        static_cast<int32_t>(static_cast<uint8_t*>(hook) - (target + 5));
    std::memset(target + 5, 0x90, N - 5);                                  // NOPs
    VirtualProtect(target, N, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, N);
    return tramp;
}

// Installs one hook, logs the result and returns the trampoline as the original function's type.
template <typename Fn, size_t N>
Fn Hook(const char* name, uintptr_t target, const uint8_t (&expected)[N], Fn hook)
{
    void* tramp = InstallHook(target, expected, reinterpret_cast<void*>(hook));
    char msg[96];
    snprintf(msg, sizeof(msg), tramp ? "Hook %s installed" : "Hook %s NOT installed (unknown Game.exe version)", name);
    Log(msg);
    return reinterpret_cast<Fn>(tramp);
}

void InstallAllHooks()
{
    g_origSetCamera    = Hook("SetCamera",    ADDR_SET_CAMERA,    PROLOGUE_SET_CAMERA,    &Hook_SetCamera);
    g_origCameraUpdate = Hook("CameraUpdate", ADDR_CAMERA_UPDATE, PROLOGUE_CAMERA_UPDATE, &Hook_CameraUpdate);
    g_origGetText      = Hook("GetText",      ADDR_GET_TEXT,      PROLOGUE_GET_TEXT,      &Hook_GetText);

    // Dialog scaling needs all of these; with any of them missing it stays off.
    // Blit is not hooked, only called; its prologue is still checked to recognise the game version.
    if (std::memcmp(reinterpret_cast<const void*>(ADDR_BLIT), PROLOGUE_BLIT, sizeof(PROLOGUE_BLIT)) == 0)
        g_gameBlit = reinterpret_cast<BlitFn>(ADDR_BLIT);
    g_origPresent       = Hook("Present",         ADDR_PRESENT,      PROLOGUE_FS_SEH,       &Hook_Present);
    g_origRunModal      = Hook("RunModal",        ADDR_RUN_MODAL,    PROLOGUE_RUN_MODAL,    &Hook_RunModal);
    g_origMouseMove     = Hook("MouseMove",       ADDR_MOUSE_MOVE,   PROLOGUE_MOUSE_MOVE,   &Hook_MouseMove);
    g_origSurfaceLookup = Hook("SurfaceLookup",   ADDR_SURFACE_FIND, PROLOGUE_SURFACE_FIND, &Hook_SurfaceLookup);
    g_origSurfaceW      = Hook("GetSurfaceWidth", ADDR_SURFACE_W,    PROLOGUE_SURFACE_SIZE, &Hook_SurfaceW);
    g_origSurfaceH      = Hook("GetSurfaceHeight", ADDR_SURFACE_H,   PROLOGUE_SURFACE_SIZE, &Hook_SurfaceH);
    g_dialogHooksOk = g_gameBlit && g_origPresent && g_origRunModal && g_origMouseMove
                   && g_origSurfaceLookup && g_origSurfaceW && g_origSurfaceH;
    Log(g_dialogHooksOk ? "Dialog scaling enabled" : "Dialog scaling disabled (a hook is missing)");
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
            swprintf_s(g_buttonLabels[i], L"%ux%u", size.width, size.height);
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

// ===== Crash report =====

// Logs the first access violation inside Game.exe: registers and every stack value that looks like a
// return address into the game's code, which gives the call chain even through hand-written asm.
// The exception is passed on, so the game still crashes as before; the log helps to find the cause.
LONG CALLBACK CrashReporter(EXCEPTION_POINTERS* info)
{
    static bool reported = false;
    const EXCEPTION_RECORD* rec = info->ExceptionRecord;
    const CONTEXT* ctx = info->ContextRecord;
    if (reported || rec->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || ctx->Eip < 0x401000 || ctx->Eip >= 0x677000)
        return EXCEPTION_CONTINUE_SEARCH;
    reported = true;

    char msg[256];
    snprintf(msg, sizeof(msg), "[Crash] EIP=%08lX %s %08lX | EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX",
             ctx->Eip, rec->ExceptionInformation[0] ? "writing" : "reading",
             static_cast<unsigned long>(rec->ExceptionInformation[1]),
             ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
    Log(msg);
    auto dm = *reinterpret_cast<uintptr_t*>(ADDR_DRAW_MANAGER);
    snprintf(msg, sizeof(msg), "[Crash] dialog active %d, draw manager size %ux%u, virtual page %d, bypass %d",
             g_dialogActive, dm ? Field<uint16_t>(dm, OFF_DM_WIDTH) : 0, dm ? Field<uint16_t>(dm, OFF_DM_HEIGHT) : 0,
             g_virtualPages[g_virtualPage], g_bypassRedirect);
    Log(msg);

    auto* stack = reinterpret_cast<const uint32_t*>(ctx->Esp);
    int len = snprintf(msg, sizeof(msg), "[Crash] stack:");
    for (int i = 0; i < 256 && len < static_cast<int>(sizeof(msg)) - 12; ++i) {
        uint32_t value = stack[i];
        if (value >= 0x401000 && value < 0x677000)
            len += snprintf(msg + len, sizeof(msg) - len, " %08X", value);
    }
    Log(msg);
    return EXCEPTION_CONTINUE_SEARCH;
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
        AddVectoredExceptionHandler(1, &CrashReporter);
    }
    return TRUE;
}
