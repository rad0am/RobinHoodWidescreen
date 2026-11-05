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
  `EnsureZoomFits` logs `zoom 0.50 -> 1.00` and the view is fine again. The fix comes one or more frames late: block the
  zoom key / the animation target instead (find where the zoom index is stepped and clamp it there).
