# Lag when opening the options menu

Clicking "Options" in the menu freezes the game for about a second.

First find out whose fault it is:
- [ ] Same click with the original game (rename `dinput.dll`): does the lag exist without the mod?
- [ ] With the mod: timestamps in our log around the options screen (`[GetText] label ...` lines, `PatchResolution`) — is our
  code slow, e.g. log flushing on every string?

Likely causes:
- [ ] Display mode enumeration (`EnumDisplayModes` through the GOG wrapper) when the resolution screen is built — slow on
  monitors with many modes. If so: hook it and return a short list, or cache the result after the first call.
- [ ] Loading/decoding the options screen graphics (bzip2 images from the `.res` files) every time it opens.
- [ ] A test surface or mode switch being created to check the available resolutions.

Tools: a sampling profiler on `Game.exe` (e.g. Visual Studio Profiler or `Very Sleepy`) during the click shows the function.
