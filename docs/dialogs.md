# Dialog scaling

Observations at 3440×1440 before the fix (screenshots from 2026-10-23):
- main menu: a fixed ~1024 px frame, centered, the rest is black,
- **pause menu**: buttons in the same place as in the main menu (right of and below the center); without the background it looks off-center,
- **briefing window (scroll)**: ~465 px, small text on a 34" screen.

## Dialogs at the size they have at height 768 — done (v0.5, 2026-11-01)
Dialogs (briefing scroll, pause menu, options, nested dialogs, mission summary) are laid out for a virtual screen of height 768
with the monitor's aspect ratio and stretched over the real screen; the map keeps the full resolution. `scale = screen height / 768`
(3440×1440 → 1832×768 ×1.875, 1920×1080 → 1368×768 ×1.406; off at 768 and below). How it works:
[reverse-engineering.md](reverse-engineering.md#dialog-scaling-v05).

Tested at 3440×1440: briefing, pause menu, options, mission summary, mouse and cursor in dialogs, map clicks after closing,
main menu after leaving a mission (unchanged, as intended).
- [x] Mission summary: opens without the window layer, so the dialog mode now starts when a known dialog start asks for the
  screen size (`DIALOG_START_CALLERS`), not at the snapshot. Giving the virtual size to all menu code in a mission also broke the
  main menu built right after a mission (the world pointer is still set then) — removed.

## Open
- [ ] Main menu and its dialogs: separate item, see [main-menu.md](main-menu.md).
- [ ] `.ini`: turn dialog scaling off, or choose the design height (768) / a fixed scale ([config-ini.md](config-ini.md)).
- [ ] Image quality of the stretch: DirectDraw/the GOG wrapper decides the filter (nearest or bilinear). See the font section below.
- [ ] Other missions: if a dialog stays small, the log names the menu code that asked for the screen size
  ("Screen size asked by menu code at ..."); add it to `DIALOG_START_CALLERS`.

## Blurry font in dialogs
Text is drawn into the 1832×768 virtual screen and then stretched ×1.875, so thin letters get soft and uneven
(a noninteger scale: some pixel columns become 2 px wide, others 1 px, or everything is blurred by bilinear filtering).
Options, from cheapest:
- [ ] Find out which filter the stretch uses (our own `Blt` or the wrapper's) and try nearest vs bilinear; compare screenshots.
- [ ] Integer scale: design height `screen height / 2` (720 at 1440) gives exactly ×2, sharp pixels. Cost: dialogs ~6 % bigger
  than designed; check the main dialog sizes still fit in 720.
- [ ] Draw text at the real resolution: find the font rendering (bitmap font in the `.res` files? GDI?) and draw it scaled with a
  bigger font instead of stretching the finished dialog. Best result, the most RE.
