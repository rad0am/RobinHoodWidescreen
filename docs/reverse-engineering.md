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
| `0x6C0FA8` | (alias) | the **same** world object as `g_World` (verified with ZoomMonitor) |

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

## Tool: ZoomMonitor

A separate console program (x86; run it as administrator, because the game uses `RUNASADMIN`) that prints the world fields
every second through `ReadProcessMemory`. It was used to confirm the addresses. The key loop:
```cpp
DWORD cam = Read<DWORD>(proc, 0x6C0FA8);
float screenW = Read<float>(proc, cam + 0x4728), screenH = Read<float>(proc, cam + 0x472C);
float mapW    = Read<float>(proc, cam + 0x4730), mapH    = Read<float>(proc, cam + 0x4734);
float zoom    = Read<float>(proc, cam + 0x475C);
float needed  = max(screenW / mapW, (screenH - 80) / mapH);
```
Output: `screen 3440x1440 | map 2944x2176 | zoom 2 | needed >= 1.16848`.

## Other data formats (from an earlier session)

- `.res` (SRES): `"SRES"`, u32 version 0x100, u32 entry count; entries `TEXT` (UTF-16LE), `PIC` (bzip2), `WAVE`.
  Polish texts: `1045\Data\Text\Level.res`. `DATA\Interface\Default.res` (BTTN, zlib) is not decoded yet.
- `.map/.min/.pak/.sxt`: `u16 w, u16 h, u32 2, u32 size` + bzip2 → RGB565.
- `.vid` = Bink. `1045\` = Polish localization (LCID 1045).
