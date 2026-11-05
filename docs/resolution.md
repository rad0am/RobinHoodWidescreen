# Dynamic resolution

**Goal:** the original, unmodified `Game.exe` + our `dinput.dll` = the game runs at the monitor's native resolution on its own.
Without this the mod cannot be distributed, because a modified `Game.exe` must not be redistributed.

Addresses and byte patterns: [reverse-engineering.md](reverse-engineering.md#resolution-patch-in-gameexe).

## Original `Game.exe`
- [x] Original `Game.exe` restored (2026-10-28; bytes at `0x12D038` checked: `68 00 00 40 44 68 00 00 80 44`). `dinput.dll` stayed in the folder.

## Patching in memory instead of in the file
- [x] `PatchResolution()` (now `resolution::Patch()`) replaces the floats of the 1024x768 menu button (`0x52CF80`):
  height immediate at VA `0x52D039`, width at VA `0x52D03E`. Original bytes are checked before writing (`PatchValue`).
- [x] New profile default (800×600 in `FUN_0055d670`): immediate at VA `0x55D763` (600.0) / `0x55D768` (800.0) → desktop resolution.
- [x] `cmp eax, 0x400` at VA `0x52D0BF` (imm `0x52D0C0`) = `FUN_0052D090`, selects the radio button for the current width
  (640/800/1024). Any other width logs "Warning, unknown screen resolution." (`RHMenuGraphics.cpp`) and selects nothing → patched to the desktop width.
- [x] Tested 2026-10-28: a clean installation starts at the desktop resolution. A resolution already saved in the settings is kept
  (by design: the player's choice wins, the native one is on the menu button). Side effect: a saved 1024x768 matches no button,
  so nothing is selected until the player picks one. Mention in the README; maybe an `.ini` option `ForceDesktop=1` ([config-ini.md](config-ini.md)).
- [x] Dropped: the engine's own path (`FUN_0055d670` takes the desktop resolution from `FUN_005e56c0` / `FUN_005e5750`
  when `g_DrawManager+0x115 != 0`). Not needed, the patches above already do this.
- [x] `0x648A58` (`and eax, 0xC00; cmp eax, 0x400`): FPU control word rounding bits in the CRT, not related to resolution.

## Reading the desktop resolution (multiple monitors)
- [x] `GetDesktopResolution()`: primary monitor, `MonitorFromPoint` → `GetMonitorInfoW` → `EnumDisplaySettingsW(ENUM_CURRENT_SETTINGS)`
  (physical mode, not `GetSystemMetrics`, which is DPI-scaled for a non-DPI-aware game). Desktops ≤ 1024 wide or < 768 high leave the menu unchanged.
- [ ] An `.ini` option to pick the monitor (index from `EnumDisplayDevicesW`) for people who play on a non-primary display. Check whether the GOG wrapper can display on a monitor other than the primary one at all.
- [ ] Cases to test: two monitors with different resolutions (e.g. 3440×1440 primary + 1920×1080), a monitor rotated to portrait, the primary monitor changing between runs, DPI scaling at 100/125/150%.
- [ ] GOG wrapper: `dxcfg.ini` has `display=3440x1440@165`. Check whether `dxcfg.exe` has a "desktop resolution" option. Otherwise, every player has to set up the wrapper by hand (document it in the instructions).

## Three buttons = three interface sizes
With `display=desktop` in `dxcfg.ini` the monitor always stays at its native mode and the wrapper scales whatever the game
renders. So the render resolution does not have to be a display mode: any size with the monitor's aspect ratio works.
The Windows mode list is not useful (most entries have a different aspect ratio → stretched image or black bars).

Plan (decided 2026-10-28): replace all three menu buttons with sizes computed from the desktop resolution, same aspect ratio:

| Button   | Meaning                                             | 3440×1440 | 3840×2160 | 1920×1080 |
|----------|-----------------------------------------------------|-----------|-----------|-----------|
| 1024x768 | native: most map                                    | 3440×1440 | 3840×2160 | 1920×1080 |
| 800x600  | middle (height 1080, or halfway when native ≤ 1080) | 2584×1080 | 1920×1080 | 1640×924  |
| 640x480  | big interface: height 768                           | 1832×768  | 1368×768  | 1368×768  |

- Height 768 instead of half (720): the interface was designed for 1024×768, so at height 768 menus, briefing and fonts have
  the original on-screen size, just wider. At 720 the main menu frame (~1024×768) probably does not fit vertically.
  Cost: scale 1.875 instead of an exact 2×, possibly slightly softer image.
- Widths rounded to a multiple of 8 (safer for old DirectDraw surface code; aspect error < 0.2 %). A button is skipped (left
  original) if its size would be < 1024×768, not smaller than native, or have the same width as another button
  (the selection compares widths only).
- [x] 2026-10-30, v0.3: `PatchResolution()` patches all three buttons (`MENU_BUTTONS` table in `src/game/addresses.h`): push immediate
  and the radio selection compares in `FUN_0052D090` (bytes verified in Game.exe). Tested in game 2026-10-30: works.
- [x] Check that the GOG wrapper accepts a non-standard mode such as 1832×768 in `SetDisplayMode` (with `display=desktop` it should).
- [x] Test in game (2026-10-30, OK): sharpness at 1.875×, main menu at height 768, how much map is visible, zoom (hook should raise it as needed).
- [x] Button labels showing the real sizes (next section).
- [ ] `.ini`: `Width=`/`Height=` override for an exact size; drop the `RenderScale` idea if the buttons cover it ([config-ini.md](config-ini.md)).

Rejected for now: a dropdown with all resolutions like in Windows display settings. The menu uses the engine's own UI
(buttons described in `Default.res`, `BTTN` block, undecoded format), so adding a new control type is a lot of RE for little gain.

## Menu labels
- [x] 2026-10-30, v0.4: labels are strings 43–45 of TEXT 1000507 (`Level.res`). The DLL hooks `Resource_GetText`
  (`0x6009E0`) and replaces them with the real sizes ("1832x768", ...) using the game's own `std::wstring::_Grow`.
  Tested in game 2026-10-30: works. Log line `[GetText] label 43 replaced with ...` confirms it.
  Details: [reverse-engineering.md](reverse-engineering.md#menu-texts-1045datatextlevelres).
- [x] Test (2026-10-30, OK): options menu labels, no crash when opening/closing the options many times, other menus unchanged.

## Risk: the saved resolution
- [ ] The resolution is saved in the settings. After removing the mod, the original game will start at 3440×1440 with the zoom bug.
  Find the settings file and either document "switch to 800x600 before uninstalling" or reset it to 1024×768 on uninstall.
