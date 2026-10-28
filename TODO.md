# TODO

Ordered by priority. Addresses and structures: [docs/reverse-engineering.md](docs/reverse-engineering.md).

---

## 1. Resolution set by the DLL (from the desktop or an `.ini` file)

**Goal:** the original, unmodified `Game.exe` + our `dinput.dll` = the game runs at the monitor's native resolution on its own.
Without this the mod cannot be distributed, because a modified `Game.exe` must not be redistributed.

### 1.1 Restore the original `Game.exe`
- [x] Original `Game.exe` restored (2026-10-28; bytes at `0x12D038` checked: `68 00 00 40 44 68 00 00 80 44`). `dinput.dll` stayed in the folder.

### 1.2 Patch in memory instead of in the file
- [x] `PatchResolution()` in `DllMain` replaces the floats of the 1024x768 menu button (`0x52CF80`):
  height immediate at VA `0x52D039`, width at VA `0x52D03E`. Original bytes are checked before writing (`PatchValue`).
- [x] New profile default (800×600 in `FUN_0055d670`): immediates at VA `0x55D763` (600.0) / `0x55D768` (800.0) → desktop resolution.
- [x] `cmp eax, 0x400` at VA `0x52D0BF` (imm `0x52D0C0`) = `FUN_0052D090`, selects the radio button for the current width
  (640/800/1024). Any other width logs "Warning, unknown screen resolution." (`RHMenuGraphics.cpp`) and selects nothing → patched to the desktop width.
- [x] Tested 2026-10-28: a clean install starts at the desktop resolution. A resolution already saved in the settings is kept
  (by design: the player's choice wins, the native one is on the menu button). Side effect: a saved 1024x768 matches no button,
  so nothing is selected until the player picks one. Mention in the README; maybe an `.ini` option `ForceDesktop=1` (1.4).
- [x] Dropped: the engine's own path (`FUN_0055d670` takes the desktop resolution from `FUN_005e56c0` / `FUN_005e5750`
  when `g_DrawManager+0x115 != 0`). Not needed, the patches above already do this.
- [x] `0x648A58` (`and eax, 0xC00; cmp eax, 0x400`): FPU control word rounding bits in the CRT, not related to resolution.

### 1.3 Reading the desktop resolution (multiple monitors)
- [x] `GetDesktopResolution()`: primary monitor, `MonitorFromPoint` → `GetMonitorInfoW` → `EnumDisplaySettingsW(ENUM_CURRENT_SETTINGS)`
  (physical mode, not `GetSystemMetrics`, which is DPI-scaled for a non-DPI-aware game). Desktops ≤ 1024 wide or < 768 high leave the menu unchanged.
- [ ] An `.ini` option to pick the monitor (index from `EnumDisplayDevicesW`) for people who play on a non-primary display. Check whether the GOG wrapper can display on a monitor other than the primary one at all.
- [ ] Cases to test: two monitors with different resolutions (e.g. 3440×1440 primary + 1920×1080), a monitor rotated to portrait, the primary monitor changing between runs, DPI scaling at 100/125/150%.
- [ ] GOG wrapper: `dxcfg.ini` has `display=3440x1440@165`. Check whether `dxcfg.exe` has a "desktop resolution" option. Otherwise every player has to set up the wrapper by hand (document it in the instructions).

### 1.4 Config file `RobinHoodWidescreen.ini`
Proposal:
```ini
[Display]
; 0 = desktop resolution of the primary monitor
Width=0
Height=0
; divisor: 1 = native, 2 = half (bigger interface, the wrapper scales 2x)
RenderScale=1

[Log]
Enabled=1
```
- [ ] Read it with `GetPrivateProfileIntW` (path next to `dinput.dll`, from `GetModuleFileNameW`).
- [ ] `RenderScale=2` gives e.g. 1720×720 at 3440×1440, the simplest way to enlarge the interface (see 2.3).

### 1.5 Menu label
- [ ] The button still says "1024x768". Find this text (UTF-16) in the `.res` files (`DATA\`, `1045\DATA\`) and replace it in memory, e.g. by hooking the function that fetches texts. Better not to distribute modified game files.

### 1.6 Risk: the saved resolution
- [ ] The resolution is saved in the settings. After removing the mod, the original game will start at 3440×1440 with the zoom bug.
  Find the settings file and either document "switch to 800x600 before uninstalling" or reset it to 1024×768 on uninstall.

---

## 2. Interface

Observations at 3440×1440 (screenshots from 2026-10-23):
- main menu: a fixed ~1024 px frame, centered, the rest is black,
- **pause menu**: buttons in the same place as in the main menu (right of and below the center); without the background it looks off-center,
- **briefing window (scroll)**: ~465 px, small text on a 34" screen,
- in-game HUD: elements anchored to the corners (OK), an 80 px black strip at the bottom between the ornaments,
- mission texts and money in the top-left corner: small.

### 2.1 Center the pause menu
- [ ] Find the code that creates the pause menu buttons: the labels "Kontynuuj" (Continue), "Zapisz" (Save), "Powtórz misję" (Restart mission) are in `.res` → text id → the code that uses it.
- [ ] Find out whether the positions are in code (constants) or in data (`DATA\Interface\Default.res`, `BTTN` block, zlib, unknown format).
- [ ] Shift the position by `(screen − 1024) / 2` or center it on the screen.

### 2.2 Bigger briefing window and fonts
- [ ] Find the drawing of the scroll (bitmap + text) and the fonts (`DATA\Interface\Fonts\MANAGER.CFG`).
- [ ] Real interface scaling in the engine is hard (every element separately). Only consider it after 2.3.

### 2.3 Simplest enlargement: render at a lower resolution (three buttons = three interface sizes)
With `display=desktop` in `dxcfg.ini` the monitor always stays at its native mode and the wrapper scales whatever the game
renders. So the render resolution does not have to be a display mode: any size with the monitor's aspect ratio works.
The Windows mode list is not useful (most entries have a different aspect ratio → stretched image or black bars).

Plan (decided 2026-10-28): replace all three menu buttons with sizes computed from the desktop resolution, same aspect ratio:

| Button | Meaning | 3440×1440 | 3840×2160 | 1920×1080 |
|---|---|---|---|---|
| 1024x768 | native: most map | 3440×1440 | 3840×2160 | 1920×1080 |
| 800x600 | middle (height 1080, or halfway when native ≤ 1080) | 2580×1080 | 1920×1080 | 1536×864 |
| 640x480 | big interface: height 768 | 1834×768 | 1366×768 | 1366×768 |

- Height 768 instead of half (720): the interface was designed for 1024×768, so at height 768 menus, briefing and fonts have
  the original on-screen size, just wider. At 720 the main menu frame (~1024×768) probably does not fit vertically.
  Cost: scale 1.875 instead of an exact 2×, possibly slightly softer image.
- Widths rounded to an even number. Skip a button (leave it original) if its size would be < 1024×768 or equal to another button.
- [ ] Patch all three buttons in `PatchResolution()`: push immediates for 640x480 at VA `0x52CFAA` / `0x52CFAF`, 800x600 at
  `0x52CFF3` / `0x52CFF8` (1024x768 already done).
- [ ] Patch the radio selection compares in `FUN_0052D090`: `cmp eax, 0x280` (VA `0x52D0A1`, imm `0x52D0A2`), `cmp eax, 0x320` (imm `0x52D0B9`); bytes checked in Game.exe: `3D 80 02 00 00` / `3D 20 03 00 00`.
- [ ] Test in game: sharpness at 1.875×, main menu at height 768, how much map is visible, zoom (hook should raise it as needed).
- [ ] Requires 1.5 (button labels showing the real sizes), otherwise players cannot tell what they choose.
- [ ] `.ini` (1.4): `Width=`/`Height=` override for an exact size; drop the `RenderScale` idea if the buttons cover it.

Rejected for now: a dropdown with all resolutions like in Windows display settings. The menu uses the engine's own UI
(buttons described in `Default.res`, `BTTN` block, undecoded format), so adding a new control type is a lot of RE for little gain.

### 2.4 Black strip at the bottom
- [ ] Check how it looked at 1024×768. Possibly stretch or tile the bar graphic between the ornaments.

---

## 3. Stability and other resolutions

- [ ] Test several missions (different map sizes). If a map is narrower than the screen even at zoom 2.0, `EnsureZoomFits` logs 2.0 as the fallback.
  Consider zoom > 2 or limiting the view. The drawing functions have a generic path for zoom > 1, but `RecordSetZoom` accepts only 0.5/1/2.
- [ ] Experiment: an intermediate zoom (e.g. 1.17 = the full map width). Check that drawing and scrolling work.
- [ ] 4K (3840×2160) and 32:9 (5120×1440): compute `needed` and test.
- [ ] Optional safety net: hook `Grid_CollectObjectsInCell` (`0x4E89D0`) with an index range check (crash at `0x4E89E1`).
- [ ] Check the zoom keys at 3440: the game blocks zooming out when the view would not fit (observed). Make sure the zoom animation (`Camera_Update` case 3) never draws a frame with an invalid zoom.

---

## 4. Release

- [ ] `git init` in `D:\Projects\RobinHoodMod` (repository for the mod and tools).
- [ ] Add ZoomMonitor to the repo as `tools/ZoomMonitor` (it is currently a separate project).
- [ ] **Release** profile in CLion (optimized, no `.ilk`/debug info). The static CRT is already set up.
- [ ] Player instructions (PL + EN): installation (copy `dinput.dll` + `.ini`), wrapper setup in `dxcfg.exe`,
  **Smart App Control** blocks unsigned DLLs, **Galaxy "Repair"**, uninstalling.
- [ ] Consider signing the DLL (certificate cost). Without a signature SAC will always block it.
- [x] Version in the log (`MOD_VERSION` in `dllmain.cpp`, `RobinHoodWidescreen 0.2 loaded`). The log is truncated on every game start.
