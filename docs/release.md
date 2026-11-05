# Release

- [ ] `git init` in `D:\Projects\RobinHoodMod` (repository for the mod and tools).
- [ ] Add ZoomMonitor to the repo as `tools/ZoomMonitor` (it is currently a separate project).
- [ ] **Release** profile in CLion (optimized, no `.ilk`/debug info). The static CRT is already set up.
- [ ] Player instructions (PL + EN): installation (copy `dinput.dll` + `.ini`), wrapper setup in `dxcfg.exe`,
  **Smart App Control** blocks unsigned DLLs, **Galaxy "Repair"**, uninstalling
  (saved resolution: [resolution.md](resolution.md#risk-the-saved-resolution)).
- [ ] Consider signing the DLL (certificate cost). Without a signature SAC will always block it.
- [x] Version in the log (`MOD_VERSION` in `src/main.cpp`). The log is truncated on every game start.
