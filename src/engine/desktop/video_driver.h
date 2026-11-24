// SPDX-License-Identifier: GPL-3.0-or-later
//
// Whether anyone sees the game's windows. SDL_VIDEODRIVER=offscreen (tests, replays, the map sweep) asks for the real
// GPU and no window on the desktop. SDL's offscreen driver has OpenGL only through EGL, which macOS lacks: there the
// request is answered by Cocoa with hidden windows, so the same scripts run on every platform.
#pragma once

#include <cstdint>

namespace sh::desktop {

// Call before SDL starts video.
void prepare_video_driver();

// True when nobody looks at the windows: SDL's offscreen or dummy driver, or Cocoa standing in for offscreen. Such a
// run has no interactive dialogs.
bool headless_video();

// SDL_WINDOW_HIDDEN where Cocoa stands in for offscreen, 0 otherwise: added to the flags of every window the game
// creates.
std::uint64_t hidden_window_flag();

}  // namespace sh::desktop
