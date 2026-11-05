// Labels of the resolution buttons: the game reads them from Level.res, the mod replaces them with
// the sizes the buttons were patched to ("1832x768", ...).
#pragma once

#include <cstddef>

namespace rhw::menu_labels {

void Install();

// New label for menu button i (index into MENU_BUTTONS); buttons without one keep the game's text.
void SetButtonLabel(size_t button, const wchar_t* text);

}  // namespace rhw::menu_labels
