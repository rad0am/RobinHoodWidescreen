# TODO

Ordered by priority. Addresses and structures: [docs/reverse-engineering.md](docs/reverse-engineering.md).

---

## 1. Resolution set by the DLL (from the desktop or an `.ini` file)

**Goal:** the original, unmodified `Game.exe` + our `dinput.dll` = the game runs at the monitor's native resolution on its own.
Without this the mod cannot be distributed, because a modified `Game.exe` must not be redistributed.

### 1.1 Restore the original `Game.exe`
- [ ] Simplest: GOG Galaxy → "Verify / Repair". It restores the original. `dinput.dll` is not a game file so it should stay, but check that.
- [ ] Alternatively `Game.exe.bak` (HxD's copy). Before using it, check that file offset `0x12D038` holds the original bytes `68 00 00 40 44 68 00 00 80 44`.

### 1.2 Patch in memory instead of in the file
- [ ] In `DllMain` (before any game code runs) replace the floats of the 1024x768 menu button (`0x52CF80`):
  - height: immediate at VA **`0x52D039`** (the `push` instruction is at `0x52D038`)
  - width: immediate at VA **`0x52D03E`** (the `push` instruction is at `0x52D03D`)
  - check the original bytes `68 00 00 40 44` / `68 00 00 80 44` before writing (same safety check as for the hooks)
  - `VirtualProtect` → write the `float` → restore the protection (as in `InstallHook`)
- [ ] Optionally also replace the default resolution of a new profile (800×600 in `FUN_0055d670`, float `push`es around VA `0x55D762`/`0x55D767`; file offsets `0x15D763`/`0x15D768`), so a new player starts at native resolution.
- [ ] **Investigate first** the engine's existing path: `FUN_0055d670` takes the desktop resolution from `FUN_005e56c0` (width) and `FUN_005e5750` (height) when `g_DrawManager+0x115 != 0`. Find in Ghidra what sets `+0x115` (maybe a command-line switch or windowed mode). Setting this flag might be enough.
- [ ] Check `cmp eax, 0x400` (1024) at VA `0x52D0BF` (right after the menu code) and `0x648A58`. It might be validation or highlighting of the selected button.

### 1.3 Reading the desktop resolution (multiple monitors)
- [ ] **Which monitor:** DirectDraw with the default device and the GOG wrapper run on the **primary monitor**, so use the primary one by default:
  `MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY)` → `GetMonitorInfoW` (`MONITORINFOEXW::szDevice`) →
  `EnumDisplaySettingsW(szDevice, ENUM_CURRENT_SETTINGS, &dm)` → `dm.dmPelsWidth` × `dm.dmPelsHeight`.
- [ ] **Do not use `GetSystemMetrics(SM_CXSCREEN)`**: the game is not DPI-aware, so with Windows scaling at 125%/150% it returns scaled-down values. `EnumDisplaySettings` returns the physical mode.
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

### 2.3 Simplest enlargement: render at a lower resolution
- [ ] Test e.g. **1720×720** (half of 3440×1440; the wrapper scales 2× without blur). Interface and fonts become 2× bigger,
  but less of the map is visible (720 px vertically instead of 768). Compromise: 2580×1080 (scale 1.33).
- [ ] For a test, patching the 800x600 button is enough: `0x12CFF5: 16→34`, `0x12CFFA: 48→D7` (720.0 / 1720.0).
- [ ] If it works well, add the `RenderScale` option to the `.ini` (item 1.4).

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
- [ ] Version in the log (e.g. `RobinHoodWidescreen 0.1 loaded`).
