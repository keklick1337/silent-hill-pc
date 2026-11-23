// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The icon of the game's windows (title bar, taskbar, dock, window switcher): assets/icon/app_<N>.png beside the
// exe, the same pictures as the exe resource on Windows and the desktop entry on Linux.
#pragma once

#include <filesystem>

struct SDL_Window;

namespace sh::desktop {

// The application's name and id for the desktop (window class, Wayland app id). Call before SDL starts video.
void set_app_identity(const char* version);

// Sets the icon of `window` from `icon_dir`; false when the pictures are not there or cannot be read (the window
// then keeps the platform's default, on Windows the exe's own resource). A video driver that has no window icons
// is not a failure.
bool set_window_icon(SDL_Window* window, const std::filesystem::path& icon_dir);

}  // namespace sh::desktop
