#include "crash_reporter.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "core/log.h"
#include "features/camera.h"
#include "features/dialog_scaling.h"
#include "game/addresses.h"

namespace rhw::crash_reporter {
namespace {

// Logs the first access violation inside Game.exe: registers and every stack value that looks like a
// return address into the game's code, which gives the call chain even through hand-written asm.
// The exception is passed on, so the game still crashes as before; the log helps to find the cause.
LONG CALLBACK CrashReporter(EXCEPTION_POINTERS* info)
{
    static bool reported = false;
    const EXCEPTION_RECORD* rec = info->ExceptionRecord;
    const CONTEXT* ctx = info->ContextRecord;
    if (reported || rec->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || ctx->Eip < GAME_CODE_BEGIN || ctx->Eip >= GAME_CODE_END)
        return EXCEPTION_CONTINUE_SEARCH;
    reported = true;

    char msg[256];
    snprintf(msg, sizeof(msg), "[Crash] EIP=%08lX %s %08lX | EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX",
             ctx->Eip, rec->ExceptionInformation[0] ? "writing" : "reading",
             static_cast<unsigned long>(rec->ExceptionInformation[1]),
             ctx->Eax, ctx->Ebx, ctx->Ecx, ctx->Edx, ctx->Esi, ctx->Edi, ctx->Ebp, ctx->Esp);
    Log(msg);
    dialogs::LogCrashState();
    camera::LogCrashState();

    auto* stack = reinterpret_cast<const uint32_t*>(ctx->Esp);
    int len = snprintf(msg, sizeof(msg), "[Crash] stack:");
    for (int i = 0; i < 256 && len < static_cast<int>(sizeof(msg)) - 12; ++i) {
        uint32_t value = stack[i];
        if (value >= GAME_CODE_BEGIN && value < GAME_CODE_END)
            len += snprintf(msg + len, sizeof(msg) - len, " %08X", value);
    }
    Log(msg);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void Install()
{
    AddVectoredExceptionHandler(1, &CrashReporter);
}

}  // namespace rhw::crash_reporter
