# Cinematic at the native resolution

`1045\Data\Cinematics\Intro.vid` and `Outro.vid` are **Bink** files (header `BIKi`), **640×320**, 25 fps, played by `binkw32.dll`.
At 3440×1440 they are shown small in the middle (to be confirmed in game).

- [ ] Check in game how they look now: centered at 640×320, or stretched by the wrapper?
- [ ] Find the playback code: imports of `binkw32.dll` in `Game.exe` (`_BinkOpen@8`, `_BinkDoFrame@4`, `_BinkCopyToBuffer@28`, ...).
  `BinkCopyToBuffer` writes the frame into a surface at given x/y; the game then blits that surface to the screen.
- [ ] Upscale: copy the frame into a 640×320 off-screen surface and stretch it with `Blt` to the screen, keeping the 2:1 aspect
  (at 3440×1440: 2880×1440, small bars left and right). The wrapper/GPU does the filtering.
- [ ] Better quality (optional): replace the files with upscaled versions (AI upscale, re-encoded to Bink). Not distributable
  (game assets), only as instructions for players.
