#include "camera.h"

#include <cstdint>

#include "core/log.h"
#include "core/patch.h"
#include "features/dialog_scaling.h"
#include "game/addresses.h"

namespace rhw::camera {
namespace {

using SetCameraFn    = void* (__cdecl*)(void* out, const float* center);
using CameraUpdateFn = int (__fastcall*)(void* self, void* edx);    // __thiscall without stack arguments
using CanZoomOutFn   = bool (__fastcall*)(void* world, void* edx);  // __thiscall, result in AL

SetCameraFn    g_origSetCamera    = nullptr;   // trampolines that run the original functions
CameraUpdateFn g_origCameraUpdate = nullptr;
CanZoomOutFn   g_origCanZoomOut   = nullptr;

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
void EnsureZoomFits(const char* caller)
{
    auto world = Global(ADDR_WORLD_PTR);
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

}  // namespace

void Install()
{
    g_origSetCamera    = Hook("SetCamera",    ADDR_SET_CAMERA,    PROLOGUE_SET_CAMERA,    &Hook_SetCamera);
    g_origCameraUpdate = Hook("CameraUpdate", ADDR_CAMERA_UPDATE, PROLOGUE_CAMERA_UPDATE, &Hook_CameraUpdate);
    g_origCanZoomOut   = Hook("CanZoomOut",   ADDR_CAN_ZOOM_OUT,  PROLOGUE_CAN_ZOOM_OUT,  &Hook_CanZoomOut);
}

}  // namespace rhw::camera
