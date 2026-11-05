#include "camera.h"

#include <windows.h>

#include <cmath>
#include <cstdint>

#include "core/log.h"
#include "core/patch.h"
#include "features/dialog_scaling.h"
#include "game/addresses.h"

namespace rhw::camera {
namespace {

using SetCameraFn    = void* (__cdecl*)(void* out, const float* center);
using CameraUpdateFn = int (__fastcall*)(void* self, void* edx);    // __thiscall without stack arguments
using GridCellFn    = void (__fastcall*)(void* world, void* edx, void* out, int cell, unsigned mask);  // __thiscall, RET 0xC
using CanZoomOutFn  = bool (__fastcall*)(void* world, void* edx);  // __thiscall, result in AL

SetCameraFn    g_origSetCamera    = nullptr;   // trampolines that run the original functions
CameraUpdateFn g_origCameraUpdate = nullptr;
CanZoomOutFn   g_origCanZoomOut   = nullptr;
GridCellFn     g_origGridCell     = nullptr;

float FF(uintptr_t world, uint32_t offset);   // finite float field for logging, defined below

// The smallest zoom at which the whole view fits on the map, or 0 when no map is loaded.
// Visible part of the map = screen / zoom, so the view fits when zoom >= screen / map.
float NeededZoom(uintptr_t world)
{
    float mapW = Field<float>(world, OFF_MAP_W);
    float mapH = Field<float>(world, OFF_MAP_H);
    if (mapW <= 0.0f || mapH <= 0.0f) return 0.0f;      // map not loaded yet
    float needX = Field<float>(world, OFF_SCREEN_W) / mapW;
    float needY = (Field<float>(world, OFF_SCREEN_H) - HUD_HEIGHT) / mapH;
    return needX > needY ? needX : needY;
}

// Raises the zoom to the smallest allowed level at which the whole view fits on the map.
void EnsureZoomFits(uintptr_t world, const char* caller)
{
    if (world == 0) return;                              // no mission loaded

    float screenW = Field<float>(world, OFF_SCREEN_W);
    float screenH = Field<float>(world, OFF_SCREEN_H);
    float mapW    = Field<float>(world, OFF_MAP_W);
    float mapH    = Field<float>(world, OFF_MAP_H);
    auto& zoom    = Field<float>(world, OFF_ZOOM);

    float needed = NeededZoom(world);
    if (needed <= 0.0f) return;                          // map not loaded yet
    if (zoom >= needed) return;                          // current zoom already fits

    // Zooming in only shrinks the view, so the camera position stays inside the map.
    float oldZoom = zoom;
    // The zoom keys and the zoom animation work with the level index, not with the float: they step the
    // index and read the zoom from the table. The index must follow the new zoom, otherwise the next
    // zoom key animates from a wrong level (after 3440 -> 1832 the map was drawn far too close).
    constexpr float levels[] = { 0.5f, 1.0f, 2.0f };
    uint16_t index = 2;                                  // fallback: the largest level the game supports
    for (uint16_t i = 0; i < 3; ++i) {
        if (levels[i] >= needed) { index = i; break; }   // the smallest level that fits
    }
    zoom = levels[index];
    auto& zoomIndex = Field<uint16_t>(world, OFF_ZOOM_IDX);
    uint16_t oldIndex = zoomIndex;
    zoomIndex = index;

    Logf("[%s] zoom %.2f -> %.2f, index %u -> %u (needed >= %.3f, screen %.0fx%.0f, map %.0fx%.0f)",
         caller, oldZoom, zoom, oldIndex, index, needed, screenW, screenH, mapW, mapH);
}

// Float field for logging: a stale or freed world holds garbage; NaN/inf are logged as -1.
float FF(uintptr_t world, uint32_t offset)
{
    float v = Field<float>(world, offset);
    return std::isfinite(v) ? v : -1.0f;
}

// Logs the map, grid and camera whenever the map or the screen size changes (a new mission, a resolution change).
void LogWorldOnce()
{
    auto world = Global(ADDR_WORLD_PTR);
    if (world == 0) return;
    static float lastW = 0, lastH = 0, lastScreen = 0;
    float w = FF(world, OFF_MAP_W), h = FF(world, OFF_MAP_H), sc = FF(world, OFF_SCREEN_W);
    if (w == lastW && h == lastH && sc == lastScreen) return;
    lastW = w; lastH = h; lastScreen = sc;
    Logf("[World] map %.0fx%.0f, grid %ux%u, screen %.0fx%.0f, zoom %.2f index %u, camera %.1f %.1f", w, h,
         Field<uint16_t>(world, OFF_GRID_W), Field<uint16_t>(world, OFF_GRID_H), sc, FF(world, OFF_SCREEN_H),
         FF(world, OFF_ZOOM), Field<uint16_t>(world, OFF_ZOOM_IDX),
         FF(world, OFF_CAM_POS), FF(world, OFF_CAM_POS + 4));
}

// The world render takes the camera position (top-left of the view, world pixels) from +0x4738/+0x473C
// (FUN_004D0A10 passes it on). At the start of a mission after another mission it can lie outside the map
// (view reaching past the map edge: garbled picture and negative grid cells in mouse picking) until the player
// scrolls and the game clamps it. Clamp it here the same way Camera_SetPositionClamped does.
void ClampCameraToMap()
{
    auto world = Global(ADDR_WORLD_PTR);
    if (world == 0) return;
    float mapW = Field<float>(world, OFF_MAP_W), mapH = Field<float>(world, OFF_MAP_H);
    float zoom = Field<float>(world, OFF_ZOOM);
    if (!(mapW > 0.0f) || !(mapH > 0.0f) || !(zoom > 0.0f)) return;
    float viewW = Field<float>(world, OFF_SCREEN_W) / zoom;
    float viewH = (Field<float>(world, OFF_SCREEN_H) - HUD_HEIGHT) / zoom;
    float& x = Field<float>(world, OFF_CAM_TOPLEFT);
    float& y = Field<float>(world, OFF_CAM_TOPLEFT + 4);
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    float maxX = mapW > viewW ? mapW - viewW : 0.0f, maxY = mapH > viewH ? mapH - viewH : 0.0f;
    float nx = x < 0.0f ? 0.0f : (x > maxX ? maxX : x);
    float ny = y < 0.0f ? 0.0f : (y > maxY ? maxY : y);
    if (nx != x || ny != y) {
        Logf("[Cam] clamp %.1f %.1f -> %.1f %.1f", x, y, nx, ny);
        x = nx; y = ny;
    }
}

// Called by the game whenever the camera is moved; center is the requested camera center.
void* __cdecl Hook_SetCamera(void* out, const float* center)
{
    EnsureZoomFits(Global(ADDR_WORLD_PTR), "SetCamera");
    return g_origSetCamera(out, center);
}

// Called by the game every frame. Catches a resolution change made while the camera is not moving.
// The original is __thiscall (this in ECX, no stack arguments); __fastcall with an unused EDX
// parameter has the same layout and can be written in plain C++.
int __fastcall Hook_CameraUpdate(void* self, void* edx)
{
    LogWorldOnce();
    EnsureZoomFits(Global(ADDR_WORLD_PTR), "CameraUpdate");
    ClampCameraToMap();
    dialogs::OnWorldFrameBegin();
    int result = g_origCameraUpdate(self, edx);
    dialogs::OnWorldFrameEnd();
    return result;
}

// Asked by the zoom out key (0x4DBDB0) before it steps the zoom index down and starts the zoom animation
// to levels[index - 1]. The game's own check works with the current zoom value (view = screen * 2 / zoom),
// which does not always match the index: the game sets the zoom without the index in some places
// (Camera_SetPositionClamped sets 1.0). After switching from 3440x1440 to 1832x768 it let the zoom go to
// 0.5, the animation drew a view wider than the map for a moment, and only then EnsureZoomFits fixed it.
// Here the level the animation will really go to has to fit as well.
// It is also asked every frame (Camera_Update 0x4C84D0, the HUD), so a block is logged only once per situation.
bool __fastcall Hook_CanZoomOut(void* worldPtr, void* edx)
{
    if (!g_origCanZoomOut(worldPtr, edx))
        return false;

    auto world = reinterpret_cast<uintptr_t>(worldPtr);
    uint16_t index = Field<uint16_t>(world, OFF_ZOOM_IDX);
    float needed = NeededZoom(world);
    float target = index > 0 && index <= 2 ? Field<float>(world, OFF_ZOOM_LEVELS + 4u * (index - 1u)) : 0.0f;
    if (needed <= 0.0f || target >= needed)
        return true;

    static float loggedZoom = 0.0f, loggedNeeded = 0.0f;
    static uint16_t loggedIndex = 0xFFFF;
    float zoom = Field<float>(world, OFF_ZOOM);
    if (zoom != loggedZoom || index != loggedIndex || needed != loggedNeeded) {
        loggedZoom = zoom; loggedIndex = index; loggedNeeded = needed;
        Logf("[CanZoomOut] blocked: zoom %.2f, index %u -> level %.2f, needed >= %.3f", zoom, index, target, needed);
    }
    return false;
}

// Safety net: a mouse pick asked for a grid cell outside the grid (mission 3 at 3440x1440: the cell array
// is read past its end -> access violation). An unreadable cell is treated as empty; the callers handle
// an empty list. The cause (a world position outside the map) is logged for further research.
void __fastcall Hook_GridCell(void* worldPtr, void* edx, void* out, int cell, unsigned mask)
{
    auto world = reinterpret_cast<uintptr_t>(worldPtr);
    auto cells = Field<uintptr_t>(world, OFF_GRID_CELLS);
    bool ok = cell >= 0 && cells != 0;
    if (ok) {
        MEMORY_BASIC_INFORMATION mbi{};
        uintptr_t begin = cells + static_cast<uintptr_t>(cell) * GRID_CELL_SIZE;
        ok = VirtualQuery(reinterpret_cast<void*>(begin), &mbi, sizeof(mbi)) != 0 &&
             mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
             begin + GRID_CELL_SIZE <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    if (!ok) {
        static int logged = 0;
        if (logged++ < 20)
            Logf("[GridCell] cell %d out of the grid (%ux%u cells) - ignored", cell,
                 Field<uint16_t>(world, OFF_GRID_W), Field<uint16_t>(world, OFF_GRID_H));
        return;
    }
    g_origGridCell(worldPtr, edx, out, cell, mask);
}

}  // namespace

void LogCrashState()
{
    auto world = Global(ADDR_WORLD_PTR);
    if (world == 0) { Log("[Crash] no world"); return; }
    Logf("[Crash] world: screen %.0fx%.0f, map %.0fx%.0f, grid %ux%u, zoom %.2f index %u, camera state %u, camera %.1f %.1f %.1f",
         FF(world, OFF_SCREEN_W), FF(world, OFF_SCREEN_H), FF(world, OFF_MAP_W), FF(world, OFF_MAP_H),
         Field<uint16_t>(world, OFF_GRID_W), Field<uint16_t>(world, OFF_GRID_H), FF(world, OFF_ZOOM), Field<uint16_t>(world, OFF_ZOOM_IDX),
         Field<uint8_t>(world, OFF_CAM_STATE), FF(world, OFF_CAM_POS), FF(world, OFF_CAM_POS + 4), FF(world, OFF_CAM_POS + 8));
}

void Install()
{
    g_origSetCamera    = Hook("SetCamera",    ADDR_SET_CAMERA,    PROLOGUE_SET_CAMERA,    &Hook_SetCamera);
    g_origCameraUpdate = Hook("CameraUpdate", ADDR_CAMERA_UPDATE, PROLOGUE_CAMERA_UPDATE, &Hook_CameraUpdate);
    g_origCanZoomOut   = Hook("CanZoomOut",   ADDR_CAN_ZOOM_OUT,  PROLOGUE_CAN_ZOOM_OUT,  &Hook_CanZoomOut);
    // The colour-key MMX blitter at 0x622550 skips only a zero width/height (OR ECX,ECX / JZ). A rect clipped
    // to a negative height made it run through memory and crash (dialog in mission 3 at 3440x1440, EDX = -2).
    // TEST + JLE skips negative sizes as well.
    // The same blitter family: 0x62261C (half size, SHR -> SAR so that a negative size stays negative) and 0x622720.
    bool blitterOk = PatchDword(0x0062255A, 0x840FC909, 0x8E0FC985) && PatchDword(0x00622562, 0x840FD209, 0x8E0FD285) &&
                     PatchDword(0x00622626, 0xEAD1E9D1, 0xFAD1F9D1) &&
                     PatchDword(0x0062262A, 0x840FC909, 0x8E0FC985) && PatchDword(0x00622632, 0x840FD209, 0x8E0FD285) &&
                     PatchDword(0x0062272A, 0x840FC909, 0x8E0FC985) && PatchDword(0x00622732, 0x840FD209, 0x8E0FD285);
    Logf("Blitter 0x622550 negative size guard %s", blitterOk ? "installed" : "NOT installed");
    g_origGridCell     = Hook("GridCell",     ADDR_GRID_CELL,     PROLOGUE_GRID_CELL,     &Hook_GridCell);
}

}  // namespace rhw::camera
