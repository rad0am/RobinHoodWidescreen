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
