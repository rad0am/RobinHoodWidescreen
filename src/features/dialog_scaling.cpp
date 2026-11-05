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

#include "dialog_scaling.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <iterator>

#include "core/log.h"
#include "core/patch.h"
#include "game/addresses.h"
#include "game/types.h"

namespace rhw::dialogs {
namespace {

using ThiscallNoArgFn = int (__fastcall*)(void* self, void* edx);    // __thiscall without stack arguments
using BlitFn          = int (__fastcall*)(void* self, void* edx, int srcId, const FloatRect* srcRect,
                                          int dstId, const FloatRect* dstRect, uint32_t flags);
using SurfaceSizeFn   = uint32_t (__fastcall*)(void* self, void* edx, int surfaceId);   // u16 in AX
using SurfaceFindFn   = void* (__fastcall*)(void* map, void* edx, const int* key);
using MouseMoveFn     = void (__cdecl*)(const float* position);
using CreateSurfaceFn = int (__fastcall*)(void* self, void* edx, uint32_t width, uint32_t height,
                                          uint32_t unknown, uint32_t keep);

BlitFn          g_gameBlit          = nullptr;   // the game's Blit, called directly (not hooked)
SurfaceSizeFn   g_origSurfaceW      = nullptr;   // trampolines that run the original functions
SurfaceSizeFn   g_origSurfaceH      = nullptr;
SurfaceFindFn   g_origSurfaceLookup = nullptr;
ThiscallNoArgFn g_origPresent       = nullptr;
ThiscallNoArgFn g_origRunModal      = nullptr;
MouseMoveFn     g_origMouseMove     = nullptr;

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
    auto dm = Global(ADDR_DRAW_MANAGER);
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
// A mission dialog always opens outside any modal loop (a dialog opened from a dialog finds the mode
// already on). A dialog start inside a modal loop is a menu dialog: the main menu after a mission is
// itself a modal loop, and its quit question uses the same scroll as the mission dialogs.
bool DialogScalingWanted()
{
    return g_dialogHooksOk && g_modalDepth == 0 && Global(ADDR_WORLD_PTR) != 0
        && GetScreenSizes().scale > 1.0f;
}

// Screen size fields of SBDrawManager. The clipping helper 0x5E6000 clips every software blit against
// them, so while a dialog draws into the virtual screen they must hold its size, or blits run past the
// end of the smaller surface (a crash in the asm blitter 0x622550).
void SetDrawManagerSize(uint16_t width, uint16_t height)
{
    auto dm = Global(ADDR_DRAW_MANAGER);
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
        auto dm = Global(ADDR_DRAW_MANAGER);
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

}  // namespace

void Install()
{
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

// Fallback for a dialog that never enters RunModal. Some dialogs (the mission summary) open during
// a world frame and the world keeps running for a moment before their modal loop starts, so the
// mode ends only after a run of world frames without a modal loop.
void OnWorldFrameBegin()
{
    if (g_dialogActive && g_modalDepth == 0 && ++g_worldFramesInDialog >= WORLD_FRAMES_TO_END_DIALOG)
        EndDialogMode("world running without a modal loop");
    ++g_cameraDepth;
}

void OnWorldFrameEnd()
{
    --g_cameraDepth;
}

void LogCrashState()
{
    auto dm = Global(ADDR_DRAW_MANAGER);
    char msg[256];
    snprintf(msg, sizeof(msg), "[Crash] dialog active %d, draw manager size %ux%u, virtual page %d, bypass %d",
             g_dialogActive, dm ? Field<uint16_t>(dm, OFF_DM_WIDTH) : 0, dm ? Field<uint16_t>(dm, OFF_DM_HEIGHT) : 0,
             g_virtualPages[g_virtualPage], g_bypassRedirect);
    Log(msg);
}

}  // namespace rhw::dialogs
