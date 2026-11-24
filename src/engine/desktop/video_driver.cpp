// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/desktop/video_driver.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>

namespace sh::desktop {

namespace {

bool offscreen_requested() {
    const char* driver = std::getenv("SDL_VIDEODRIVER");
    return driver != nullptr && std::strcmp(driver, "offscreen") == 0;
}

}  // namespace

void prepare_video_driver() {
#ifdef __APPLE__
    // The environment variable outranks a hint of normal priority. The variable itself stays, so a restarted game
    // (engine/app/restart.cpp) makes the same choice.
    if (offscreen_requested()) SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "cocoa", SDL_HINT_OVERRIDE);
#endif
}

bool headless_video() {
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver != nullptr && (std::strcmp(driver, "offscreen") == 0 || std::strcmp(driver, "dummy") == 0)) return true;
#ifdef __APPLE__
    return offscreen_requested();
#else
    return false;
#endif
}

std::uint64_t hidden_window_flag() {
#ifdef __APPLE__
    return offscreen_requested() ? SDL_WINDOW_HIDDEN : 0;
#else
    return 0;  // the windows of SDL's own offscreen driver reach no screen anyway
#endif
}

}  // namespace sh::desktop
