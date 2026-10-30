# Reverse engineering: `Game.exe` (GOG 1.1.0.0)

All addresses are **virtual addresses (VA)** unless marked "file offset".
Function names (`SetVideoMode`, `Camera_Update`, ...) are our own, assigned in the Ghidra project `RobinHoodMod`.

## File and address conversion

- `Game.exe`: 2,899,968 bytes, version 1.1.0.0, PE timestamp `0x3E257C94` (January 2003), x86 32-bit, MSVC.
- `ImageBase = 0x400000`, no ASLR, so addresses are the same on every run.

| Section | VA | RawOffset | RawSize |
|---|---|---|---|
| `.text` | `0x001000` | `0x001000` | `0x276000` |
| `.rdata` | `0x277000` | `0x277000` | `0x02F000` |
| `.data` | `0x2A6000` | `0x2A6000` | `0x01B000` |
| `.rsrc` | `0x2C4000` | `0x2C1000` | `0x003000` |

**VA = 0x400000 + section_VA + (file_offset − section_RawOffset).** For `.text`, `.rdata` and `.data`: **VA = file_offset + 0x400000**.

Imports (`Symbol Tree → Imports`): ADVAPI32, AVIFIL32, BINKW32, DDRAW (2 functions), **DINPUT (1: `DirectInputCreateEx`)**,
FMOD (45), GDI32, KERNEL32, OLE32, USER32, WINMM (1).

## Engine

The engine is Spellbound's **SBLibNG**. The exe still contains source file paths such as
`P:\Robin_Hood\SBLibNG\SBDrawManager...`, `P:\Robin_Hood\rhshadowpolygon.cpp` and `P:\Robin_Hood\RHScript.cpp`,
which helps a lot with naming classes.

## Globals

| Address | Name | Meaning |
|---|---|---|
| `0x6C0E48` | `g_App` | application **object** (not a pointer). `[g_App+0x28]` = settings object (`GetSettings`) |
| `0x6C11D0` | `g_DrawManager` | pointer to `SBDrawManager` (`this` for `SetVideoMode`) |
| `0x6C0A88` | `g_World` | pointer to the world/mission object (0 in menus) |
| `0x6C0FA8` | (alias) | the **same** world object as `g_World` (verified at runtime) |

## Structures (confirmed fields)

### Settings: `[0x6C0E48 + 0x28]`
| Offset | Type | Meaning |
|---|---|---|
| `+0x54` | float | width of the selected resolution |
| `+0x58` | float | height of the selected resolution |

The resolution is **saved to disk** together with the settings (file not identified yet).

### `SBDrawManager` (`g_DrawManager`)
| Offset | Type | Meaning |
|---|---|---|
| `+0x80` | byte | 1 = fullscreen |
| `+0x88` | `IDirectDraw7*` | vtable: `+0x18` CreateSurface, `+0x54` SetDisplayMode |
| `+0x10E` | u16 | current width |
| `+0x110` | u16 | current height |
| `+0x115` | byte | when ≠ 0, the default resolution is the **desktop resolution** (see `FUN_0055d670`) |

### World (`[0x6C0A88]` = `[0x6C0FA8]`)
| Offset | Type | Meaning |
|---|---|---|
| `+0x403C` | ptr | spatial grid cell array, 0x28-byte elements (`+0x10` u16 object count, `+0x14` list) |
| `+0x4728` | float | screen width (px) |
| `+0x472C` | float | screen height (px) |
| `+0x4730` | float | map width (px). Mission 1: 2944 |
| `+0x4734` | float | map height (px). Mission 1: 2176 |
| `+0x475C` | float | **zoom**: 0.5 (zoomed out), 1.0, 2.0 (zoomed in) |
| `+0x4760` | float | previous zoom |
| `+0x4898` | u16 | index into the zoom transition table |
| `+0x489C`, `+0x48A0` | float[] | zoom transition table (from / to) |
| `+0x48C0` | byte | camera state machine state (`switch` in `Camera_Update`, case 3 = zoom animation) |
| `+0x4B3C` | byte | flag checked while drawing |
| `+0x4BD0..+0x4BD8` | float×3 | probably the camera position (x, y, height) |

The interface bar at the bottom of the screen is **80 px** tall and the map is not drawn under it (constant `80.0` in `Camera_SetPositionClamped`).

## Functions

| Address | Name | Convention | Description |
|---|---|---|---|
| `0x40A230` | (main) | | main game function: DirectX check, `Data\*` folders, videos, menu/mission loop |
| `0x5E1440` | (DX version) | | DirectX version detection; `< 0x700` → "Please Install a newer version of DirectX". It checks the exports of `dinput.dll`, so the proxy must export A, W and Ex |
| `0x5E4810` | `SetVideoMode` | `__thiscall`, `RET 0xC` | `(this, u16 width, u16 height, int bpp)`. No 1024/768 constants inside, accepts any size |
| `0x55DBB0` | `GetSettings` | `__fastcall` | `return this->[+0x28]` |
| `0x52EE20` | `ApplyVideoSettings` | | reads floats `+0x54/+0x58`, `__ftol`, `SetVideoMode(g_DrawManager, w, h, 16)` |
| `0x52CF80` | (resolution menu) | `__thiscall` | 3 buttons (`this+0x640/+0x754/+0x868`) → `FUN_005fc3c0(w, h)` (floats) → settings. Change flag `+0xDC8` |
| `0x55D670` | (settings init) | | no saved settings → 800×600, or the desktop resolution (`FUN_005e56c0` width, `FUN_005e5750` height) when `g_DrawManager+0x115` is set |
| `0x5758D0` | `Camera_SetPositionClamped` | `__cdecl`, `RET` | `(Vec2* out, const Vec2* center)`. Clamps the camera to the map. **Bug:** when the view is larger than the map it sets zoom = 1.0 without checking. **Hooked** |
| `0x4C8380` | `Camera_Update` | `__thiscall` (ECX) | camera update every frame (states, zoom animation, calls the renderer). **Hooked** |
| `0x4D0A10` | (world render) | | draws the world; zoom = `[this+0x475C]` is passed to every drawing function |
| `0x4D7F40` | `World_Draw` | | `(this, ?, float zoom)` |
| `0x58E7F0` | `ShadowPolygon_Draw` | | character shadow / view cone; message "unable to zoom in because of too small map" when the position is < 0 |
| `0x572D50` | `RecordSetZoom` (script) | | only 0.5 / 1.0 / 2.0 are allowed |
| `0x4E89D0` | `Grid_CollectObjectsInCell` | `__thiscall` | **crash site** `0xC0000005` at `0x4E89E1`: cell index outside the grid when the view is larger than the map |

Callers of `SetVideoMode`: `0x40B029, 0x40B146, 0x40B48C, 0x40B535, 0x40B79B` (main: 640×480 for the `Intro.vid`/`Outro.vid` videos,
then back to the settings), `0x52EECF` (ApplyVideoSettings), `0x537445, 0x537489, 0x5392FC`.

Callers of `Camera_SetPositionClamped`: `0x4C00F7, 0x4C0239, 0x4C5E8C, 0x4C5F00, 0x4CA646, 0x4CA6E9, 0x4CA786, 0x5712D4, 0x571394, 0x571442`.

Writes to the zoom (`+0x475C`): `FUN_004be6d0` (mission start / load?), `FUN_004c0040` and `FUN_004da390` (temporarily 1.0, then restored),
`Camera_Update` (5×), `Camera_SetPositionClamped` (2× `= 1.0`). In addition, `LEA EAX,[EDI+0x475C]` in `FUN_004c4570` passes the zoom's address on.

## Resolution patch in `Game.exe`

The menu (`0x52CF80`) passes the resolution as `push float` (`68` + 4 bytes), height first, then width:

| Button | File offset | VA | Original | Patch (currently in Game.exe) |
|---|---|---|---|---|
| 640x480 | `0x12CFA9` / `0x12CFAE` | `0x52CFA9` / `0x52CFAE` | `68 00 00 F0 43` / `68 00 00 20 44` | unchanged |
| 800x600 | `0x12CFF2` / `0x12CFF7` | `0x52CFF2` / `0x52CFF7` | `68 00 00 16 44` / `68 00 00 48 44` | unchanged |
| 1024x768 | `0x12D038` / `0x12D03D` | `0x52D038` / `0x52D03D` | `68 00 00 40 44` / `68 00 00 80 44` | `68 00 00 B4 44` (1440.0) / `68 00 00 57 45` (3440.0) |

Since 2026-10-28 `Game.exe` is original again; `PatchResolution()` in the DLL writes the new sizes in memory
(since v0.3 all three buttons: 640x480 → height 768, 800x600 → height 1080, 1024x768 → native, all with the desktop's aspect ratio):

| What | VA (immediate) | Original |
|---|---|---|
| 640x480 button height / width | `0x52CFAA` / `0x52CFAF` | 480.0f / 640.0f |
| 800x600 button height / width | `0x52CFF3` / `0x52CFF8` | 600.0f / 800.0f |
| 1024x768 button height / width | `0x52D039` / `0x52D03E` | 768.0f / 1024.0f |
| Radio selection in `FUN_0052D090`: `cmp eax, 0x280` / `0x320` / `0x400` | `0x52D0A2` / `0x52D0B9` / `0x52D0C0` | 640 / 800 / 1024 (u32). Unknown width → "Warning, unknown screen resolution." |
| New profile default height / width (`FUN_0055d670`) | `0x55D763` / `0x55D768` | 600.0f / 800.0f |

`FUN_0052D090` (radio selection): `fld [this+0xDC0]` (the menu's copy of the selected width) → `__ftol` → compares
**only the width** with 640 / 800 / 1024. So every button must have a distinct width.

### Other resolution constants (searched 2026-10-30, `.text` only)

Byte-pattern search over Game.exe for the integer and float forms of 640/800/1024/480/600/768:

| Pattern | Hits | Notes |
|---|---|---|
| `cmp eax, 0x280` | `0x52D0A1` only | radio selection |
| `cmp eax, 0x320` | `0x52D0B8`, **`0x5A8107`** | `0x5A8107`: `cmp eax, 800; jae ...` in an unknown function, maybe a layout/asset choice for widths ≥ 800. Not a problem for our sizes (all ≥ 1024), worth a look in Ghidra |
| `cmp eax, 0x400` | `0x52D0BF`, `0x648A58` | `0x648A58` = CRT FPU control word, unrelated |
| float 768.0 / 1024.0 | `0x52D039` / `0x52D03E` only | 1024x768 button. **No** UI code uses 1024×768 as a constant |
| float 600.0 + 800.0 pairs | `0x51B9A6`/`0x51B9AB`, `0x52CFF3`/`0x52CFF8`, `0x55D763`/`0x55D768` | `0x51B9A0`: a constructor (`mov esi, ecx; push 600.0; push 800.0`) — probably the settings object's default before loading. Not patched (the loaded/initialized value overrides it) |
| float 480.0 + 640.0 pairs `push 480.0; push 640.0; push 0; push 0; call` | `0x52C61D`, `0x52D7BE`, `0x52E51F`, `0x53083D`, `0x536D9D`, `0x5388CE`, `0x53EA4E`, `0x53FE3F`, `0x541AE1` (+ `0x52CFAA` button) | a **rectangle (0, 0, 640, 480)** built in many menu screens (`0x52C...`–`0x541...` = the `RHMenu*` code). Candidate for a virtual 640×480 coordinate space of menu dialogs → lead for TODO 2.1/2.2 (pause menu, briefing) |
| integer 640 (`80 02 00 00`) | 36 hits | includes `0x40B797` (`push 280h` for the intro/outro videos at 640×480) — not touched by the mod |

## Menu texts (`1045\Data\Text\Level.res`)

The resolution button labels are **not** in Game.exe (no ASCII/UTF-16 hit), only in the Polish `Level.res`, at file offset
`0x367AF5` (offsets below point at the length prefix): a list of length-prefixed UTF-16 strings (`u16 length` + characters, no terminator), not aligned to 2 bytes:
`"Rozdzielczość"` (len 13), `"640x480"` (`0x367B0D`), `"800x600"` (`0x367B1D`), `"1024x768"` (`0x367B2D`), then
`"Efekty specjalne"`, `"Pole widzenia alfa"`, `"Przezroczyste cienie"`, `"Efekty animacyjne"`, ... (the options screen).
The entry that holds this list has not been identified yet (the simple 3-string `TEXT` layout does not match here).

Float bytes: `[BitConverter]::GetBytes([single]3440)`. 3440 = `0x45570000`, 1440 = `0x44B40000`, 3840 = `0x45700000`,
2160 = `0x45070000`, 1720 = `0x44D70000`, 720 = `0x44340000`.

## The hook (how it works in `dllmain.cpp`)

Both hooked functions start with the MSVC SEH prologue:
```
6A FF            PUSH -1
68 xx xx xx xx   PUSH <handler>      ; 0x5758D0: 0x0066B31F, 0x4C8380: 0x0065C672
```
These 7 bytes do not depend on where they are in memory. The hook replaces them with `E9 rel32` (JMP to our function) + `90 90`.
The trampoline (executable memory from `VirtualAlloc`) holds the copied 7 bytes + `JMP target+7`.
Before overwriting, the bytes are compared with the expected ones, so a different game version is left untouched.

## Other data formats (from an earlier session)

- `.res` (SRES): `"SRES"`, u32 version 0x100, u32 entry count; entries `TEXT` (UTF-16LE), `PIC` (bzip2), `WAVE`.
  Polish texts: `1045\Data\Text\Level.res`. `DATA\Interface\Default.res` (BTTN, zlib) is not decoded yet.
- `.map/.min/.pak/.sxt`: `u16 w, u16 h, u32 2, u32 size` + bzip2 → RGB565.
- `.vid` = Bink. `1045\` = Polish localization (LCID 1045).
