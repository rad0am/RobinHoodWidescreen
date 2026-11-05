// RobinHoodWidescreen - native desktop resolution for "Robin Hood: The Legend of Sherwood"
// (GOG, Game.exe 1.1.0.0).
//
// The game ships as a dinput.dll proxy: Windows loads this DLL instead of the
// system one, we install our hooks, and forward all DirectInput calls to the
// real system DLL (dinput_proxy.cpp).
//
// What it does:
//   - the resolution menu offers sizes with the desktop's aspect ratio, native included (features/resolution);
//   - zoom fix: the view never gets wider than the map (features/camera);
//   - dialogs (briefing, pause menu, options, ...) keep the size they have at height 768 (features/dialog_scaling);
//   - the black strip of the bottom bar is filled with the foliage ornament (features/bar_fill).

#include <windows.h>

#include "core/log.h"
#include "crash_reporter.h"
#include "features/bar_fill.h"
#include "features/camera.h"
#include "features/dialog_scaling.h"
#include "features/menu_labels.h"
#include "features/resolution.h"

namespace {

constexpr const char* MOD_VERSION = "0.5.4";

}  // namespace

// Runs when Windows loads the DLL, before any game code. Only light work is allowed here
// (the loader lock is held), so the real dinput.dll is loaded later, on first use.
// user32 is safe to call: we import it, so Windows initializes it before our DllMain.
// extern "C": the CRT startup code calls DllMain by its undecorated C name.
extern "C" BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        rhw::Logf("RobinHoodWidescreen %s loaded", MOD_VERSION);
        rhw::resolution::Patch();
        rhw::camera::Install();
        rhw::menu_labels::Install();
        rhw::dialogs::Install();
        rhw::bar_fill::Install();
        rhw::crash_reporter::Install();
    }
    return TRUE;
}
