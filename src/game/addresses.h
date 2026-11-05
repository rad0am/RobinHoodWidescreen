// Addresses, structure offsets and function prologues in Game.exe (GOG, version 1.1.0.0).
// Details and how they were found: docs/reverse-engineering.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace rhw {

// ===== Functions and globals =====
constexpr uintptr_t ADDR_SET_CAMERA    = 0x005758D0;   // Camera_SetPositionClamped (cdecl)
constexpr uintptr_t ADDR_CAMERA_UPDATE = 0x004C8380;   // Camera_Update (this in ECX), runs every frame
constexpr uintptr_t ADDR_CAN_ZOOM_OUT  = 0x004C7CF0;   // World_CanZoomOut (this = world in ECX) -> AL, asked by the zoom out key
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

// Game.exe code range: return addresses and crash locations inside the game.
constexpr uintptr_t GAME_CODE_BEGIN = 0x00401000;
constexpr uintptr_t GAME_CODE_END   = 0x00677000;

// ===== Fields of SBDrawManager =====
constexpr uint32_t OFF_DM_SURFACES = 0x54;    // std::map<int, Surface>: surface id -> DirectDraw surface
constexpr uint32_t OFF_DM_WIDTH    = 0x10E;   // u16, current screen width
constexpr uint32_t OFF_DM_HEIGHT   = 0x110;   // u16, current screen height

// ===== Fields of the world object =====
constexpr uint32_t OFF_SCREEN_W = 0x4728;   // float, screen width in pixels
constexpr uint32_t OFF_SCREEN_H = 0x472C;   // float, screen height in pixels
constexpr uint32_t OFF_MAP_W    = 0x4730;   // float, map width in pixels
constexpr uint32_t OFF_MAP_H    = 0x4734;   // float, map height in pixels
constexpr uint32_t OFF_ZOOM     = 0x475C;   // float, 0.5 / 1.0 / 2.0
constexpr uint32_t OFF_ZOOM_IDX = 0x4898;   // u16, index of the zoom level in the table at +0x489C (0..2); the zoom keys change it
constexpr uint32_t OFF_ZOOM_LEVELS = 0x489C;   // float[3], zoom levels 0.5 / 1.0 / 2.0
constexpr float    HUD_HEIGHT   = 80.0f;    // bottom interface bar, the map is not drawn under it

// ===== Resolution constants (operands of PUSH / CMP instructions) =====
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
inline constexpr MenuButton MENU_BUTTONS[] = {
    { "640x480",   640, 480, 0x0052CFAA, 0x0052CFAF, 0x0052D0A2 },
    { "800x600",   800, 600, 0x0052CFF3, 0x0052CFF8, 0x0052D0B9 },
    { "1024x768", 1024, 768, 0x0052D039, 0x0052D03E, 0x0052D0C0 },
};
constexpr size_t MENU_BUTTON_COUNT = std::size(MENU_BUTTONS);
// The button labels are strings 43/44/45 of the interface text table (TEXT 1000507 in Level.res).
constexpr uint32_t UI_TEXT_ID           = 1000507;
constexpr uint32_t UI_TEXT_FIRST_BUTTON = 43;
// Settings init (FUN_0055D670), default for a new profile: push 600.0f; push 800.0f
constexpr uintptr_t ADDR_DEFAULT_HEIGHT = 0x0055D763;
constexpr uintptr_t ADDR_DEFAULT_WIDTH  = 0x0055D768;

// ===== Function prologues replaced by the hooks =====
// The first bytes of each hooked function are copied to a trampoline, so they must be whole
// instructions that do not depend on where they are (no relative jumps or calls). They are also
// compared before patching, so a different Game.exe version is left untouched.
//
// Standard MSVC SEH prologue, 7 bytes: PUSH -1; PUSH <exception handler>
inline constexpr uint8_t PROLOGUE_SET_CAMERA[7]    = { 0x6A, 0xFF, 0x68, 0x1F, 0xB3, 0x66, 0x00 };
inline constexpr uint8_t PROLOGUE_CAMERA_UPDATE[7] = { 0x6A, 0xFF, 0x68, 0x72, 0xC6, 0x65, 0x00 };
inline constexpr uint8_t PROLOGUE_BLIT[7]          = { 0x6A, 0xFF, 0x68, 0xC8, 0x28, 0x67, 0x00 };
// World_CanZoomOut, 6 bytes: FLD dword [ECX+0x475C]
inline constexpr uint8_t PROLOGUE_CAN_ZOOM_OUT[6]  = { 0xD9, 0x81, 0x5C, 0x47, 0x00, 0x00 };
// Present reads the SEH list first, 8 bytes: MOV EAX, FS:[0]; PUSH -1
inline constexpr uint8_t PROLOGUE_FS_SEH[8]        = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x6A, 0xFF };
// Resource_GetText, 7 bytes: SUB ESP, 0x10; LEA EAX, [ESP+0x14]
inline constexpr uint8_t PROLOGUE_GET_TEXT[7]      = { 0x83, 0xEC, 0x10, 0x8D, 0x44, 0x24, 0x14 };
// GetSurfaceWidth/Height, 7 bytes: MOV EAX, [ESP+4]; SUB ESP, 8
inline constexpr uint8_t PROLOGUE_SURFACE_SIZE[7]  = { 0x8B, 0x44, 0x24, 0x04, 0x83, 0xEC, 0x08 };
// Surface map lookup, 8 bytes: PUSH ECX; PUSH ESI; MOV ESI, ECX; LEA ECX, [ESP+4]
inline constexpr uint8_t PROLOGUE_SURFACE_FIND[8]  = { 0x51, 0x56, 0x8B, 0xF1, 0x8D, 0x4C, 0x24, 0x04 };
// RunModal, 8 bytes: SUB ESP, 0x10; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI, ECX
inline constexpr uint8_t PROLOGUE_RUN_MODAL[8]     = { 0x83, 0xEC, 0x10, 0x53, 0x55, 0x56, 0x8B, 0xF1 };
// Mouse move, 8 bytes: SUB ESP, 0x14; MOV ECX, 0x6C12E8 (an absolute address, fine to copy)
inline constexpr uint8_t PROLOGUE_MOUSE_MOVE[8]    = { 0x83, 0xEC, 0x14, 0xB9, 0xE8, 0x12, 0x6C, 0x00 };

}  // namespace rhw
