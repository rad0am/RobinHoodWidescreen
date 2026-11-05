#pragma once

namespace rhw {

// Appends a line to RobinHoodWidescreen.log in the game folder. The first call of each run
// truncates the file, so the log only holds the current session.
void Log(const char* text);

}  // namespace rhw
