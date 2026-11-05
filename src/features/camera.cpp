#include "camera.h"

#include <cstdint>
#include <cstdio>

#include "core/log.h"
#include "core/patch.h"
#include "features/dialog_scaling.h"
#include "game/addresses.h"

namespace rhw::camera {
namespace {

using SetCameraFn    = void* (__cdecl*)(void* out, const float* center);
using CameraUpdateFn = int (__fastcall*)(void* self, void* edx);    // __thiscall without stack arguments

SetCameraFn    g_origSetCamera    = nullptr;   // trampolines that run the original functions
CameraUpdateFn g_origCameraUpdate = nullptr;

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

    if (mapW <= 0.0f || mapH <= 0.0f) return;            // map not loaded yet

    // Visible part of the map = screen / zoom, so the view fits when zoom >= screen / map.
    float needX  = screenW / mapW;
    float needY  = (screenH - HUD_HEIGHT) / mapH;
    float needed = needX > needY ? needX : needY;

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

    char msg[200];
    snprintf(msg, sizeof(msg), "[%s] zoom %.2f -> %.2f, index %u -> %u (needed >= %.3f, screen %.0fx%.0f, map %.0fx%.0f)",
             caller, oldZoom, zoom, oldIndex, index, needed, screenW, screenH, mapW, mapH);
    Log(msg);
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

}  // namespace

void Install()
{
    g_origSetCamera    = Hook("SetCamera",    ADDR_SET_CAMERA,    PROLOGUE_SET_CAMERA,    &Hook_SetCamera);
    g_origCameraUpdate = Hook("CameraUpdate", ADDR_CAMERA_UPDATE, PROLOGUE_CAMERA_UPDATE, &Hook_CameraUpdate);
}

}  // namespace rhw::camera
