#include "log.h"

#include <cstdarg>
#include <cstdio>

namespace rhw {

void Log(const char* text)
{
    static bool firstCall = true;
    FILE* f = nullptr;
    if (fopen_s(&f, "RobinHoodWidescreen.log", firstCall ? "w" : "a") == 0 && f) {
        firstCall = false;
        fprintf(f, "%s\n", text);
        fclose(f);
    }
}

void Logf(const char* format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Log(text);
}

}  // namespace rhw
