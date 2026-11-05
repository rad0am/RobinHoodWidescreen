# Zoom, stability and other resolutions

Zoom functions and structures: [reverse-engineering.md](reverse-engineering.md#functions).

- [ ] Test several missions (different map sizes). If a map is narrower than the screen even at zoom 2.0, `EnsureZoomFits` logs 2.0 as the fallback.
  Consider zoom > 2 or limiting the view. The drawing functions have a generic path for zoom > 1, but `RecordSetZoom` accepts only 0.5/1/2.
- [ ] Experiment: an intermediate zoom (e.g. 1.17 = the full map width). Check that drawing and scrolling work.
- [ ] 4K (3840×2160) and 32:9 (5120×1440): compute `needed` and test.
- [ ] Optional safety net: hook `Grid_CollectObjectsInCell` (`0x4E89D0`) with an index range check (crash at `0x4E89E1`).
- [ ] Check the zoom keys at 3440: the game blocks zooming out when the view would not fit (observed). Make sure the zoom animation
  (`Camera_Update` case 3) never draws a frame with an invalid zoom.
  Observed 2026-11-05: after switching from 3440×1440 to 1832×768, zooming out to 0.5 garbles the image for a moment, then
  `EnsureZoomFits` logs `zoom 0.50 -> 1.00` and the view is fine again. The fix came one or more frames late.
- [x] v0.5.3 (tested in game 2026-11-05, OK): `World_CanZoomOut` (`0x4C7CF0`) is hooked. The game checks `screen × 2 / zoom` against the map with
  the current zoom value, but the zoom out key (`0x4DBDB0`) animates to `levels[index - 1]`; when the zoom value and the index
  disagree the check passes for a level that does not fit. The hook also requires `levels[index - 1] >= needed`.
  Log line `[CanZoomOut] blocked: ...` shows when it acts. If the garbling still happens, the zoom comes from another path
  (callers of `0x4C7CF0`: `0x4C01D8`, `0x4C84D0` in `Camera_Update`, `0x4C9159`, `0x4CE879`, `0x50E213`, `0x5143E2`).

## v0.5.5 - mission 3 after mission 2 (tested 2026-11-05)
- At the start of a mission after another mission the camera (`world + 0x4738/0x473C`, top-left of the view, passed to
  `FUN_004D0A10`) can lie outside the map (mission 3: `-1520, -272`): garbled picture until the player scrolls, negative cells
  in `Grid_CollectObjectsInCell`. `ClampCameraToMap` clamps it to `[0, map - view]` every frame. Loading a save was fine.
- `Camera_SetPositionClamped` only *returns* the clamped position in its out parameter, it does not move the camera.
- `Grid_CollectObjectsInCell` (`0x4E89D0`) is `RET 0xC` (3 stack arguments: out, cell, mask). The hook ignores unreadable cells.
- Dialog crash at 3440x1440: the colour-key MMX blitters `0x622550`, `0x62261C`, `0x622720` only skipped a zero width/height,
  a negative one (rect clipped to the virtual 1832x768 screen) ran through memory. Patched to skip negative sizes
  (`TEST`/`JLE`, `SHR` -> `SAR` in `0x62261C`). Cause not found: something in a dialog is drawn outside the virtual screen.
- Build as Release: the Debug CRT asserts (`log10 failed`) on garbage floats read from a freed world.
- Mission-init hook at `0x4BED98` was tried: the map size is still 0 there, so it was dropped.
