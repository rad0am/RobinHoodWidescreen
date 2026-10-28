# RobinHoodWidescreen

A mod for **Robin Hood: The Legend of Sherwood** (GOG release, `Game.exe` 1.1.0.0, Polish) that makes the game playable
at widescreen and UHD resolutions (tested: 3440×1440, 21:9 ultrawide).

## Status (2026-10-26)

| Item | Status |
|---|---|
| Game running at 3440×1440 (native rendering, not upscaling) | ✅ works, but via a `Game.exe` patch made by hand in HxD |
| Zoom fix (map split into bands, crash when zoomed out) | ✅ works, via the `dinput.dll` built by this project |
| Resolution set by the DLL (no changes to `Game.exe`) | ❌ to do, see [TODO.md](TODO.md) |
| Resolution taken from the desktop / an `.ini` file | ❌ to do |
| Interface: pause menu, briefing window, fonts | ❌ to do |
| Package for other players (Release build, instructions) | ❌ to do |

## How it works

1. **Resolution:** the options menu code (`0x52CF80`) sets the resolution for its three buttons with constant floats
   (`push 768.0f; push 1024.0f`). Replacing those 2×4 bytes with 1440.0f/3440.0f makes the "1024x768" button start
   3440×1440. The engine (`SetVideoMode`, `0x5E4810`) accepts any size.
2. **Zoom:** the game has 3 zoom levels (0.5 / 1.0 / 2.0). The camera function (`0x5758D0`) forces zoom 1.0 when the view
   does not fit on the map, assuming 1.0 always fits. At 3440 px width that is false (the mission 1 map is 2944×2176), which
   caused the bands and the crash. The mod hooks this function and `Camera_Update` (`0x4C8380`) and, before they run,
   raises the zoom to the smallest level that fits.
3. **Loading:** the mod is a **`dinput.dll` proxy**. The game imports `DINPUT.DLL`, and Windows looks in the game folder first,
   so it loads our DLL. It installs the hooks in `DllMain` and forwards DirectInput calls to the system `dinput.dll`.

Details: [docs/reverse-engineering.md](docs/reverse-engineering.md). Work log: [docs/journal.md](docs/journal.md).

## Files

| File | Contents |
|---|---|
| `dllmain.cpp` | the whole mod: hooks, zoom logic, DirectInput proxy, `DllMain` |
| `exports.def` | undecorated exports: `DirectInputCreateA`, `DirectInputCreateW`, `DirectInputCreateEx` |
| `CMakeLists.txt` | build: 32-bit, static CRT, output file `dinput.dll` |
| `docs/` | reverse engineering notes and work log |
| `TODO.md` | remaining work |

## Building

Requirements: **Build Tools for Visual Studio** (workload "Desktop development with C++") and CLion with a
**Visual Studio toolchain, Architecture: x86**. `CMakeLists.txt` stops the build if the toolchain is 64-bit.

- CLion: **Build → Build Project** (Ctrl+F9) → `cmake-build-debug\dinput.dll`
- From a console (without CLion):
  ```bat
  "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
  "C:\Program Files\JetBrains\CLion 2026.2.4\bin\cmake\win\x64\bin\cmake.exe" --build cmake-build-debug
  ```
- Check the exports (Developer PowerShell for VS): `dumpbin /exports cmake-build-debug\dinput.dll`.
  There must be 3 undecorated names (no leading `_`, no `@16`/`@20`).

## Installing and testing (current state)

1. `Game.exe` must contain the resolution patch (see [docs/reverse-engineering.md](docs/reverse-engineering.md#resolution-patch-in-gameexe)).
2. Copy `dinput.dll` to `E:\GogLibrary\Robin Hood - The Legend of Sherwood\`.
3. Start the game **through GOG Galaxy** (Galaxy adds the compatibility flags `DWM8And16BitMitigation 8And16BitAggregateBlts RUNASADMIN`).
4. The mod log is `RobinHoodWidescreen.log` in the game folder. A correct start looks like this:
   ```
   RobinHoodWidescreen loaded
   Hook SetCamera installed
   Hook CameraUpdate installed
   [SetCamera] zoom 0.50 -> 2.00 (needed >= 1.168, screen 3440x1440, map 2944x2176)
   ```
5. Uninstalling: delete `dinput.dll` from the game folder.

## Important notes

- **Smart App Control** (Windows 11) blocks unsigned DLLs. With SAC enabled, the game will not start with the mod installed.
- **GOG Galaxy "Verify / Repair"** restores the original `Game.exe` (undoing the resolution patch). Also turn off automatic updates for the game.
- Backups in the game folder: `Game.exe.bak` (HxD's copy from before the first patch, most likely the original).
  Backups from step "10b": `DATA\Configuration.orig`, `DATA\Savegame.orig` (if they were made).
- The chosen resolution is saved in the game settings. After removing the mod or the patch, the game may try to start at the saved 3440×1440.
  Switch to 800x600 in the options before uninstalling.
- The GOG wrapper (`ddraw.dll`, configured in `dxcfg.ini`) is set to `display=3440x1440@165`, `scaling=fit`, `aspect=enabled`.
