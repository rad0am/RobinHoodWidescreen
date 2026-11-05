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
