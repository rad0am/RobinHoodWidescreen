#include "menu_labels.h"

#include <cstdint>
#include <cstdio>
#include <cwchar>

#include "core/log.h"
#include "core/patch.h"
#include "game/addresses.h"
#include "game/types.h"

namespace rhw::menu_labels {
namespace {

using GetTextFn     = int (__fastcall*)(void* self, void* edx, uint32_t id, uint32_t index, GameWString* out);
using WStringGrowFn = bool (__fastcall*)(GameWString* self, void* edx, uint32_t length, bool trim);

GetTextFn g_origGetText = nullptr;

// New labels ("1832x768", ...) for the patched buttons; empty = keep the game's text.
wchar_t g_buttonLabels[MENU_BUTTON_COUNT][16] = {};

// Replaces the text of a game string, the same way the game's own GetUIText (0x5E23D0) does it.
void SetGameString(GameWString* str, const wchar_t* text)
{
    static const auto grow = reinterpret_cast<WStringGrowFn>(ADDR_WSTRING_GROW);
    auto length = static_cast<uint32_t>(wcslen(text));
    if (!grow(str, nullptr, length, true))
        return;
    wmemcpy(str->ptr, text, length);
    str->ptr[length] = L'\0';
    str->length = length;
}

// Called by the game for every text it reads from a .res file (menus, missions, dialogs).
// The original is __thiscall with 3 stack arguments (RET 0xC); __fastcall with an unused EDX
// has the same layout. Returns nonzero when the text was found.
int __fastcall Hook_GetText(void* self, void* edx, uint32_t id, uint32_t index, GameWString* out)
{
    int found = g_origGetText(self, edx, id, index, out);
    uint32_t button = index - UI_TEXT_FIRST_BUTTON;   // wraps around for indexes below 43
    if (found && id == UI_TEXT_ID && button < MENU_BUTTON_COUNT && g_buttonLabels[button][0]) {
        SetGameString(out, g_buttonLabels[button]);

        static bool logged[MENU_BUTTON_COUNT] = {};
        if (!logged[button]) {
            logged[button] = true;
            char msg[96];
            snprintf(msg, sizeof(msg), "[GetText] label %u replaced with %ls", index, g_buttonLabels[button]);
            Log(msg);
        }
    }
    return found;
}

}  // namespace

void Install()
{
    g_origGetText = Hook("GetText", ADDR_GET_TEXT, PROLOGUE_GET_TEXT, &Hook_GetText);
}

void SetButtonLabel(size_t button, const wchar_t* text)
{
    if (button < MENU_BUTTON_COUNT)
        wcsncpy_s(g_buttonLabels[button], text, _TRUNCATE);
}

}  // namespace rhw::menu_labels
