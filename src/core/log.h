#pragma once

#include <sal.h>

namespace rhw {

// Appends a line to RobinHoodWidescreen.log in the game folder. The first call of each run
// truncates the file, so the log only holds the current session.
void Log(const char* text);

// Log with printf formatting; lines longer than 511 characters are cut.
void Logf(_Printf_format_string_ const char* format, ...);

}  // namespace rhw
