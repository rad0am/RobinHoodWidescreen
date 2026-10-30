# RobinHoodWidescreen

A mod for **Robin Hood: The Legend of Sherwood** (GOG release, `Game.exe` 1.1.0.0, Polish) that makes the game playable
at widescreen and UHD resolutions (tested: 3440×1440, 21:9 ultrawide). The original `Game.exe` stays unmodified:
everything is done in memory by `dinput.dll`.

## Status (2026-10-30, version 0.3)

| Item | Status |
|---|---|
| Game running at the desktop resolution (native rendering, not upscaling) | ✅ works, set by the DLL, `Game.exe` unmodified |
| Zoom fix (map split into bands, crash when zoomed out) | ✅ works |
| More resolution choices (three buttons sized from the monitor) | ✅ works (v0.3). Labels still say 640x480 / 800x600 / 1024x768 |
| `.ini` file (custom resolution, monitor choice) | ❌ to do |
| Interface: pause menu, briefing window, fonts | ❌ to do |
| Package for other players (Release build, instructions) | ❌ to do |

## How it works

1. **Loading:** the mod is a **`dinput.dll` proxy**. The game imports `DINPUT.DLL`, and Windows looks in the game folder first,
   so it loads our DLL. In `DllMain` it applies the patches below and forwards DirectInput calls to the system `dinput.dll`.
2. **Resolution:** the options menu code (`0x52CF80`) sets the resolution for its three buttons with constant floats
   (`push 768.0f; push 1024.0f`). The DLL reads the physical resolution of the primary monitor (`EnumDisplaySettingsW`)
   and writes it in memory into:
   - the "1024x768" button (the label still says 1024x768),
   - the check that marks the selected button (otherwise the game logs "unknown screen resolution" and selects nothing),
   - the default resolution of a new profile (800×600), so a clean install starts at the desktop resolution.

   A resolution already saved in the game settings is kept; the player can switch with the "1024x768" button.
   The engine (`SetVideoMode`, `0x5E4810`) accepts any size.
3. **Zoom:** the game has 3 zoom levels (0.5 / 1.0 / 2.0). The camera function (`0x5758D0`) forces zoom 1.0 when the view
   does not fit on the map, assuming 1.0 always fits. At 3440 px width that is false (the mission 1 map is 2944×2176), which
   caused the bands and the crash. The mod hooks this function and `Camera_Update` (`0x4C8380`) and, before they run,
   raises the zoom to the smallest level that fits.

Every patch first compares the original bytes, so with a different `Game.exe` version nothing is changed (the log says so).

Details: [docs/reverse-engineering.md](docs/reverse-engineering.md).

## Files

| File | Contents |
|---|---|
| `dllmain.cpp` | the whole mod: resolution patches, hooks, zoom logic, DirectInput proxy, `DllMain` |
| `exports.def` | undecorated exports: `DirectInputCreateA`, `DirectInputCreateW`, `DirectInputCreateEx` |
| `CMakeLists.txt` | build: 32-bit, static CRT, output file `dinput.dll` |
| `docs/reverse-engineering.md` | addresses, structures and findings |
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

## Installing and testing

1. Copy `dinput.dll` to the game folder (here `E:\GogLibrary\Robin Hood - The Legend of Sherwood\`). `Game.exe` must be the original one.
2. GOG wrapper (`ddraw.dll`, configured with `dxcfg.exe` / `dxcfg.ini`): `display=desktop`, `scaling=fit`, `aspect=enabled`.
3. Start the game **through GOG Galaxy** (Galaxy adds the compatibility flags `DWM8And16BitMitigation 8And16BitAggregateBlts RUNASADMIN`).
4. If a resolution was saved earlier, pick the "1024x768" button in the options: it now sets the desktop resolution.
5. The mod log is `RobinHoodWidescreen.log` in the game folder, cleared on every start. A correct start looks like this:
   ```
   RobinHoodWidescreen 0.3 loaded
   Resolution: desktop 3440x1440; 640x480 -> 1832x768 patched, 800x600 -> 2584x1080 patched, 1024x768 -> 3440x1440 patched, new profile default patched
   Hook SetCamera installed
   Hook CameraUpdate installed
   [SetCamera] zoom 1.00 -> 2.00 (needed >= 1.168, screen 3440x1440, map 2944x2176)
   ```
   The `zoom` line appears only when the mod has to change the zoom.
6. Uninstalling: switch the resolution to 800x600 in the options, then delete `dinput.dll` (see the notes below).

## Important notes

- **Smart App Control** (Windows 11) blocks unsigned DLLs. With SAC enabled, the game will not start with the mod installed.
- **The chosen resolution is saved in the game settings.** After removing the mod, the original game would start at the saved
  desktop resolution with the zoom bug. Switch to 800x600 in the options before uninstalling.
- GOG Galaxy "Verify / Repair" does not remove `dinput.dll` (it is not a game file).
- `Game.exe.bak` in the game folder is HxD's copy from before the first manual patch. It is no longer needed.
