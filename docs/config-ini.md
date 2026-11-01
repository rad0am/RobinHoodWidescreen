# Config file `RobinHoodWidescreen.ini`

Not implemented yet. Proposal:
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
- [ ] `RenderScale=2` gives e.g. 1720×720 at 3440×1440. Probably not needed: the three resolution buttons already give
  three interface sizes ([resolution.md](resolution.md#three-buttons--three-interface-sizes)).
- [ ] `Width=`/`Height=` override for an exact render size.
- [ ] `ForceDesktop=1`: ignore a resolution saved in the settings and start at the desktop resolution.
- [ ] Monitor index (from `EnumDisplayDevicesW`) for people who play on a non-primary display.
- [ ] Dialog scaling: turn it off, or choose the design height (768) / a fixed scale ([dialogs.md](dialogs.md)).
