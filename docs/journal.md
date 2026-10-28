# Work log

A short history: how we got to the current state and what we learned along the way.

## 2026-10-23: reconnaissance

1. Monitor: 3440×1440 (21:9 ultrawide, not 4K).
2. Game folder: `Game.exe` (the game), `Robin Hood.exe` (autorun launcher), GOG wrapper `ddraw.dll` + `dxcfg.ini` (already set to 3440×1440, `scaling=fit`).
   Conclusion: the wrapper only scales the image; a real higher resolution needs a change in `Game.exe`.
3. Searching for `push 768; push 1024` (int) → 0 hits. Found `push 16; push 480; push 640` in 2 places.
   Both `CALL`s go to `0x5E4810` (computed from the relative `E8` jump).
4. PE header → formula VA = file offset + 0x400000.
5. Ghidra: `0x5E4810` = `SetVideoMode` (IDirectDraw vtable `+0x54`), no constants inside. `RET 0xC` → 3 parameters.
6. Callers use `__ftol`, so the resolution is stored as **floats**. Settings: `[g_App+0x28]+0x54/+0x58`.
7. Float search: pairs 480/640, 600/800 and 768/1024, 5 bytes apart = `push float` in the menu at `0x52CF80`.

## 2026-10-23: first patch

8. HxD: the 1024x768 button → 3440×1440. **Works.** The menu is a fixed ~1024 px frame centered on screen; the HUD is anchored to the corners.
9. Problem: at mission start the map is drawn as 3 misaligned bands. Fine after zooming in.

## 2026-10-24: zoom

10. 800×600 + zoomed out + switch to 3440 → crash `0xC0000005` at `0x4E89E1` (grid index outside the array).
11. Strings in the exe: "Shadow polygon : unable to zoom in because of too small map", `RecordSetZoom` (zoom only 0.5/1/2).
12. The main renderer passes `[world+0x475C]` as the zoom (hypothesis).

## 2026-10-25: first C++ and confirmation

13. CLion + Build Tools (x86). ZoomMonitor (`ReadProcessMemory`) confirmed: zoom = `[0x6C0A88]+0x475C`.
14. Scalar search for `0x475c` → 12 writes. `0x5758D0` compares the zoom with 0.5 and forces 1.0.
15. Analysis of `0x5758D0` = `Camera_SetPositionClamped`: when the view is larger than the map, it sets zoom 1.0 without checking. **Root cause.**
16. ZoomMonitor: map 2944×2176; at 3440×1440 the needed zoom is ≥ 1.168 → only 2.0 works.

## 2026-10-26: the DLL mod

17. `dinput.dll` proxy (the game imports only `DirectInputCreateEx`; `winmm` was rejected because the wrapper/SDL use it too).
18. Error "Please install a newer version of DirectX": the DirectX version check looks up `DirectInputCreateA` with `GetProcAddress`.
    Lesson: a proxy must export **all** functions of the original.
19. The mod did nothing because `DllMain` was missing from the code. After adding `DllMain` and a second hook (`Camera_Update`, every frame):
    ```
    [SetCamera] zoom 0.50 -> 2.00 (needed >= 1.168, screen 3440x1440, map 2944x2176)
    ```
    **The zoom fix works.**

## Lessons

- Static analysis (Ghidra) gives a hypothesis; dynamic analysis (ZoomMonitor, the log) confirms it.
- Strings left in the exe (error messages, source paths) are the fastest way into the code.
- Calling conventions: `RET n` tells how many bytes of arguments a function removes. `__stdcall`/`__thiscall` must match to the byte.
- A crash is often a symptom: the out-of-range index came from a view larger than the map, not from a bug in the grid.
- Proxy DLL: full path to the real system DLL, nothing heavy in `DllMain` (loader lock), exports via `.def` (x86 decorates names).
