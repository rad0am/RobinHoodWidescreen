// At a resolution wider than 1024 the bottom bar of the mission screen has the foliage ornament only in
// the two corners; between them the bar is black. The game draws the corner pieces as three surfaces: a
// 320 px piece in each corner and a 160x110 piece right of the left one (all bottom aligned). This module
// repeats the 160x110 piece across the black part.
//
// The HUD is drawn by the UI manager (0x6189E0), called once per mission frame from 0x510618. The fill is
// drawn just before it, so the portraits and the corner pieces stay on top. The surface id of the 160x110
// piece is not fixed: it is learned from the Blit that draws it (the first frames of a mission have no fill).

#include "bar_fill.h"

#include <windows.h>
#include <cstdint>
#include <intrin.h>

#include "core/log.h"
#include "core/patch.h"
#include "game/addresses.h"
#include "game/types.h"

namespace rhw::bar_fill {
namespace {

constexpr uintptr_t ADDR_UI_DRAW = 0x006189E0;                   // UI manager draw, __thiscall (int), RET 4
constexpr uintptr_t UI_DRAW_FROM_MISSION_FRAME = 0x00510618;     // return address of the mission's HUD draw

constexpr float CORNER_W = 320.0f;                               // corner pieces
constexpr float TILE_W = 160.0f, TILE_H = 110.0f;                // the piece that is repeated

using UiDrawFn = int (__fastcall*)(void* self, void* edx, int arg);
using BlitFn   = int (__fastcall*)(void* self, void* edx, int srcId, const FloatRect* srcRect,
                                   int dstId, const FloatRect* dstRect, uint32_t flags);

UiDrawFn g_uiDraw = nullptr;
BlitFn   g_blit   = nullptr;

bool  g_inMissionHud = false;   // the mission HUD is being drawn
int   g_tileSurface = 0;        // surface of the 160x110 piece, 0 = not seen yet
float g_tileBottom = 0.0f;      // bottom edge of the piece = screen height

void DrawFill(void* drawManager)
{
    if (!g_tileSurface) return;
    float end = static_cast<float>(Field<uint16_t>(reinterpret_cast<uintptr_t>(drawManager), OFF_DM_WIDTH)) - CORNER_W;
    for (float x = CORNER_W + TILE_W; x < end; x += TILE_W) {
        float width = end - x < TILE_W ? end - x : TILE_W;
        FloatRect src{ 0.0f, 0.0f, width, TILE_H };
        FloatRect dst{ x, g_tileBottom - TILE_H, x + width, g_tileBottom };
        g_blit(drawManager, nullptr, g_tileSurface, &src, 0, &dst, 1);   // flags 1: colour key, like the game's own pieces
    }
}

int __fastcall Hook_UiDraw(void* self, void* edx, int arg)
{
    bool mission = reinterpret_cast<uintptr_t>(_ReturnAddress()) == UI_DRAW_FROM_MISSION_FRAME;
    if (!mission)
        return g_uiDraw(self, edx, arg);
    DrawFill(reinterpret_cast<void*>(Global(ADDR_DRAW_MANAGER)));
    g_inMissionHud = true;
    int result = g_uiDraw(self, edx, arg);
    g_inMissionHud = false;
    return result;
}

int __fastcall Hook_Blit(void* self, void* edx, int srcId, const FloatRect* src, int dstId, const FloatRect* dst,
                         uint32_t flags)
{
    // Learn the ornament piece: 160x110, right of the 320 px corner piece, drawn whole.
    if (g_inMissionHud && !src && dst && dst->left == CORNER_W && dst->right - dst->left == TILE_W &&
        dst->bottom - dst->top == TILE_H && (g_tileSurface != srcId || g_tileBottom != dst->bottom)) {
        g_tileSurface = srcId;
        g_tileBottom = dst->bottom;
        Logf("Bar fill: ornament surface %d, bottom %.0f", srcId, dst->bottom);
    }
    return g_blit(self, edx, srcId, src, dstId, dst, flags);
}

}  // namespace

void Install()
{
    g_blit   = Hook("BarFillBlit", ADDR_BLIT,    PROLOGUE_BLIT,   &Hook_Blit);
    g_uiDraw = Hook("BarFillUiDraw", ADDR_UI_DRAW, PROLOGUE_FS_SEH, &Hook_UiDraw);
    if (!g_blit || !g_uiDraw)
        Log("Bar fill disabled (a hook is missing)");
}

}  // namespace rhw::bar_fill
