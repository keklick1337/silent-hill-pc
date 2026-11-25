// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The display's refresh as a phone has it: a screen of 120 Hz (or 90, 144) that the system runs at whatever rate
// the shown windows ask for. The game asks for its frame cap, so the display's own rhythm paces the frames (FIFO
// present) instead of a 120 Hz screen showing a 60 fps game at uneven intervals. A desktop does nothing here.

#include <vector>

namespace sh::platform {

// The refresh rates (Hz, rounded, ascending) the display offers at its current resolution; empty when unknown (a
// desktop, or before the window).
const std::vector<int>& display_rates();

// The frame caps the Options row offers on a phone: 30, 60, and 120 when the display can run at 120 Hz.
std::vector<int> phone_frame_caps();

// Asks the system to run the display at `fps` while the game's window shows (Android: the surface's frame rate as a
// fixed source, ANativeWindow_setFrameRate); 0 withdraws the request. Called every frame: only a new rate or a new
// surface (the app was paused) asks again.
void request_frame_rate(int fps);

}  // namespace sh::platform
