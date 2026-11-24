// SPDX-License-Identifier: GPL-3.0-or-later
// Whether the running screen lets the player skip now: every reader of the skip button (a movie, the boot logos, a
// map event's skippable scene, the staff roll) notes that it asked. The touch screen shows its Skip button while
// someone asks, so the button appears only where the original lets the player skip.
#pragma once

namespace sh::input {

void note_skip_asked();
// True when a reader asked within the last `ms` milliseconds.
bool skip_asked_within(int ms);

}  // namespace sh::input
