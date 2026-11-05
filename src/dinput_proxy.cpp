// dinput.dll proxy: Windows loads this DLL instead of the system one (it sits next to Game.exe),
// and every DirectInput call is forwarded to the real system DLL.

#include <windows.h>

#include "core/log.h"

namespace {

// Loads the real system dinput.dll once and returns the address of one of its functions.
// The full path is required: LoadLibrary("dinput.dll") would return this proxy again.
FARPROC GetRealProc(const char* name)
{
    static HMODULE real = nullptr;
    if (!real) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);             // System32 (redirected to SysWOW64 for 32-bit)
        wcscat_s(path, L"\\dinput.dll");
        real = LoadLibraryW(path);
        if (!real) {
            rhw::Log("ERROR: could not load the system dinput.dll");
            return nullptr;
        }
    }
    FARPROC proc = GetProcAddress(real, name);
    if (!proc)
        rhw::Logf("ERROR: %s not found in the system dinput.dll", name);
    return proc;
}

}  // namespace

// All three creation functions are exported (see exports.def), because the game's DirectX
// version check looks them up with GetProcAddress. Signatures must match the originals exactly:
// WINAPI is __stdcall, so the callee removes its arguments from the stack.

extern "C" HRESULT WINAPI DirectInputCreateA(HINSTANCE hinst, DWORD version, LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, LPVOID*, LPUNKNOWN);
    static auto real = reinterpret_cast<Fn>(GetRealProc("DirectInputCreateA"));
    return real ? real(hinst, version, out, outer) : E_FAIL;
}

extern "C" HRESULT WINAPI DirectInputCreateW(HINSTANCE hinst, DWORD version, LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, LPVOID*, LPUNKNOWN);
    static auto real = reinterpret_cast<Fn>(GetRealProc("DirectInputCreateW"));
    return real ? real(hinst, version, out, outer) : E_FAIL;
}

extern "C" HRESULT WINAPI DirectInputCreateEx(HINSTANCE hinst, DWORD version, REFIID riid,
                                              LPVOID* out, LPUNKNOWN outer)
{
    using Fn = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
    static auto real = reinterpret_cast<Fn>(GetRealProc("DirectInputCreateEx"));
    return real ? real(hinst, version, riid, out, outer) : E_FAIL;
}
