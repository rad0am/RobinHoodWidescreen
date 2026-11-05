# In-game HUD

The HUD is not a dialog, so [dialog scaling](dialogs.md) does not cover it; it is still drawn at the real size.

Observations at 3440×1440 (screenshots from 2026-10-23):
- elements anchored to the corners (OK),
- an 80 px black strip at the bottom between the ornaments,
- mission texts and money in the top-left corner: small,
- minimap: drawn at its original pixel size, too small to read on a 34" screen.

## Bottom bar
- [ ] Enlarge the bar (character portraits, buttons) by the same `screen height / 768` scale as dialogs. Find where it is drawn
  and whether it uses the same screen-size query as the dialogs (then the dialog mode trick may work for it too).
- [ ] Mouse hit-testing of the bar must use the scaled positions (same problem already solved for dialogs).
- [ ] Black strip at the bottom: check how it looked at 1024×768. Possibly stretch or tile the bar graphic between the ornaments.

## Corner texts
- [ ] Money and mission objectives in the top-left corner: scale like the bar.

## Minimap
- [ ] Find the minimap drawing code and its rectangle (size is probably a constant or comes from `Default.res`).
- [ ] Scale it up (×1.875 at 1440). Clicking the minimap moves the camera, so the click → map position conversion must be scaled too.
- [ ] Check that the view rectangle drawn on the minimap still matches the wider screen (it already shows more map than at 1024).

## Black strip — done (v0.5.4, 2026-11-05)
The part of the bar between the corner pieces is filled with the 160x110 foliage ornament repeated across it (`src/features/bar_fill.cpp`).
The corner pieces are three surfaces drawn by the UI manager: 320 px in each corner and a 160x110 piece right of the left one; the id of that
piece is learned from the Blit that draws it. The fill is drawn right before the UI manager, so everything else stays on top.

## HUD scaling experiment (not shipped)
Goal was a bigger bar (×1.875 at 1440). It worked visually but was dropped; findings for a later attempt:
- Layout: `RHGame` lays all widgets out in `0x50A670` (`__thiscall`, u16 width, u16 height, `RET 8`); calling it with the virtual size
  (1832x768) makes everything virtual. The hero slots (`RHWidgetPortrait`, `0x5D6940`) ignore the arguments and read the real size from
  the settings floats (`[0x6C0E48+0x28]+0x54/+0x58`), so they needed a hook that swapped the settings for the call.
- Drawing: the UI manager (`0x6189E0`, called from `0x510618`) draws straight onto the back buffer with `Blit` (images and font glyphs from
  `0x61B2DC`); scaling the destination rect in a `Blit` hook stretches them (colour key survives). The cursor is drawn inside it
  (`0x627FB0`) and must not be scaled. The back buffer is surface id 0 or 1.
- The map is not drawn under the bar: 18 `FSUB [0x677598]` (80.0f) and 23 `push -80.0f` sites in the camera and world code must follow the bar
  height, otherwise the scrolling map cache glitches (e.g. `0x4C0414 … 0x575A58`, `0x4C16F2 … 0x4DA6A2`).
- Mouse: UI hit tests use the global mouse position (`0x6C1300`) and the UI cursor (`manager+0x1758`, `+0x80`), both real; widgets in virtual
  coordinates need a virtual position inside the UI input call (`0x6192A0`, `RET 0xC`) and a position off screen elsewhere.
- Unsolved: the hero names and data next to the slots (small yellow text) were drawn at the virtual slot positions without going through
  `Blit`, the software blitters (`0x5EAA40`, `0x5EC5F0`) or GDI `TextOut`/`DrawText` hooks, so they stayed unscaled in the middle of the map.
  The slot object (`0x5D6940`) stores dozens of rectangle fields that differ between the real and the virtual layout.
