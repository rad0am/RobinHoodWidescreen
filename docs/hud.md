# In-game HUD

The HUD is not a dialog, so [dialog scaling](dialogs.md) does not cover it; it is still drawn at the real size.

Observations at 3440×1440 (screenshots from 2026-10-23):
- elements anchored to the corners (OK),
- an 80 px black strip at the bottom between the ornaments (filled since v0.5.4, see below),
- mission texts and money in the top-left corner: small,
- minimap: drawn at its original pixel size, too small to read on a 34" screen.

## Corner texts
- [ ] Money and mission objectives in the top-left corner: make them bigger.

## Minimap
- [ ] Find the minimap drawing code and its rectangle (size is probably a constant or comes from `Default.res`).
- [ ] Scale it up (×1.875 at 1440). Clicking the minimap moves the camera, so the click → map position conversion must be scaled too.
- [ ] Check that the view rectangle drawn on the minimap still matches the wider screen (it already shows more map than at 1024).

## Black strip — done (v0.5.4, 2026-11-05)
The part of the bar between the corner pieces is filled with the 160x110 foliage ornament repeated across it (`src/features/bar_fill.cpp`).
The corner pieces are three surfaces drawn by the UI manager: 320 px in each corner and a 160x110 piece right of the left one; the id of that
piece is learned from the Blit that draws it. The fill is drawn right before the UI manager, so everything else stays on top.

