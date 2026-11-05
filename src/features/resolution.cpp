#include "resolution.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "core/log.h"
#include "core/patch.h"
#include "features/menu_labels.h"
#include "game/addresses.h"

namespace rhw::resolution {
namespace {

// Physical resolution of the primary monitor. DirectDraw and the GOG wrapper use the primary
// monitor. GetSystemMetrics(SM_CXSCREEN) is not used: the game is not DPI aware, so with
// Windows scaling at 125%/150% it would return scaled-down values.
bool GetDesktopResolution(DWORD& width, DWORD& height)
{
    HMONITOR monitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        return false;

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        return false;

    width  = mode.dmPelsWidth;
    height = mode.dmPelsHeight;
    return true;
}

// Render size with the desktop's aspect ratio for the given height. The width is rounded to a
// multiple of 8 (old DirectDraw code and the GOG wrapper are happier with aligned surfaces);
// the aspect ratio error is far below what is visible.
uint32_t WidthForHeight(DWORD desktopW, DWORD desktopH, uint32_t height)
{
    double width = static_cast<double>(desktopW) * height / desktopH;
    return static_cast<uint32_t>(std::lround(width / 8.0)) * 8;
}

// Points one menu button at a new resolution: both push immediate and the selection compare.
bool PatchMenuButton(const MenuButton& button, uint32_t width, uint32_t height)
{
    // The compare is checked first, so a different Game.exe version fails before anything is written.
    if (std::memcmp(reinterpret_cast<void*>(button.addrSelect), &button.width, sizeof(uint32_t)) != 0)
        return false;
    return PatchDword(button.addrHeight, Bits(static_cast<float>(button.height)), Bits(static_cast<float>(height)))
        && PatchDword(button.addrWidth,  Bits(static_cast<float>(button.width)),  Bits(static_cast<float>(width)))
        && PatchDword(button.addrSelect, button.width, width);
}

}  // namespace

// Replaces the three menu resolutions with sizes that have the desktop's aspect ratio, and the
// new-profile default (800x600) with the native size.
//   "640x480"  -> height 768: the interface (designed for 1024x768) keeps its original on-screen size
//   "800x600"  -> height 1080 (or halfway between 768 and native on smaller desktops)
//   "1024x768" -> native desktop resolution: the most map, the smallest interface
// The GOG wrapper (display=desktop, scaling=fit) keeps the monitor in its native mode and scales
// the smaller sizes up, so they do not have to be real display modes.
void Patch()
{
    DWORD desktopW = 0, desktopH = 0;
    if (!GetDesktopResolution(desktopW, desktopH)) {
        Log("Resolution: could not read the desktop resolution, menu left unchanged");
        return;
    }
    if (desktopW <= 1024 || desktopH < 768) {
        Log("Resolution: desktop is not larger than 1024x768, menu left unchanged");
        return;
    }

    uint32_t middleH = desktopH > 1080 ? 1080 : (desktopH + 768) / 2 / 2 * 2;
    struct Size { uint32_t width, height; };
    const Size sizes[] = {
        { WidthForHeight(desktopW, desktopH, 768), 768 },
        { WidthForHeight(desktopW, desktopH, middleH), middleH },
        { desktopW, desktopH },
    };

    char msg[400];
    int len = snprintf(msg, sizeof(msg), "Resolution: desktop %lux%lu;", desktopW, desktopH);
    uint32_t usedWidths[3] = {};
    for (size_t i = 0; i < MENU_BUTTON_COUNT; ++i) {
        const MenuButton& button = MENU_BUTTONS[i];
        Size size = sizes[i];

        // The selection compares widths only, so every button needs its own width. A size smaller
        // than the interface (1024x768) or not smaller than native (except the native button) is skipped.
        bool isNative  = i == MENU_BUTTON_COUNT - 1;
        bool tooSmall  = size.width < 1024 || size.height < 768;
        bool notSmaller = !isNative && size.height >= desktopH;
        bool duplicate = std::ranges::find(usedWidths, size.width) != std::end(usedWidths);

        const char* result;
        if (tooSmall || notSmaller || duplicate) {
            result = "skipped";
        } else if (PatchMenuButton(button, size.width, size.height)) {
            usedWidths[i] = size.width;
            wchar_t label[16];
            swprintf_s(label, L"%ux%u", size.width, size.height);
            menu_labels::SetButtonLabel(i, label);
            result = "patched";
        } else {
            result = "NOT patched";
        }
        len += snprintf(msg + len, sizeof(msg) - len, " %s -> %ux%u %s,", button.label, size.width, size.height, result);
    }

    uint32_t w = Bits(static_cast<float>(desktopW));
    uint32_t h = Bits(static_cast<float>(desktopH));
    bool defaults = PatchDword(ADDR_DEFAULT_HEIGHT, Bits(600.0f), h) && PatchDword(ADDR_DEFAULT_WIDTH, Bits(800.0f), w);
    snprintf(msg + len, sizeof(msg) - len, " new profile default %s", defaults ? "patched" : "NOT patched");
    Log(msg);
}

}  // namespace rhw::resolution
