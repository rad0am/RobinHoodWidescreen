#include "log.h"

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

}  // namespace rhw
