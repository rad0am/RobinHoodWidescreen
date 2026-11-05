// Dialogs (briefing, pause menu, options, ...) keep the size they have at height 768.
// How it works: the comment at the top of dialog_scaling.cpp and docs/reverse-engineering.md.
#pragma once

namespace rhw::dialogs {

// Installs the hooks; dialog scaling stays off when any of them is missing.
void Install();

// Called around every world frame (Camera_Update).
void OnWorldFrameBegin();
void OnWorldFrameEnd();

// Logs the dialog mode state after a crash.
void LogCrashState();

}  // namespace rhw::dialogs
