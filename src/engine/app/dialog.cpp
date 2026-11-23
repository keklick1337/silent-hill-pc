// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/dialog.h"

#include <SDL3/SDL_messagebox.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// -1: not decided yet (the environment decides on first use), 0/1: set.
int g_console = -1;

bool console() {
    if (g_console < 0) {
        const char* env = std::getenv("SH_DIALOGS_TO_CONSOLE");
        g_console = (env != nullptr && env[0] != '\0' && std::strcmp(env, "0") != 0) ? 1 : 0;
    }
    return g_console != 0;
}

}  // namespace

extern "C" void ShDialog_SetConsole(int on) { g_console = on != 0 ? 1 : 0; }

extern "C" void ShDialog_Show(int error, const char* title, const char* text) {
    std::fprintf(stderr, "[DIALOG] %s: %s: %s\n", error != 0 ? "error" : "info", title != nullptr ? title : "",
                 text != nullptr ? text : "");
    std::fflush(stderr);
    if (console()) {
        return;
    }
    SDL_ShowSimpleMessageBox(error != 0 ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, title, text, nullptr);
}
