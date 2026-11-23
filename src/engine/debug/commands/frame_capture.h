// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Screenshots of the running game, read from the renderer's composed frame (not from the OS window, so they
// work offscreen and carry no window borders).

#pragma once

#include <string>

namespace sh::debug {

// Saves the composed frame as a PNG, or with `whole_window` the picture the window shows (the frame where the
// present step places it, black around it). An empty path names the file screenshot-<ticks>.png in the working
// directory. Returns the line that tells the outcome ("shot: <path> (<w>x<h>, <n> bytes)" or the reason).
std::string save_screenshot(const std::string& path, bool whole_window);

}  // namespace sh::debug
