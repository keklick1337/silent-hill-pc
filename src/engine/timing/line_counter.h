// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The horizontal-blank counter the game's frame clock reads, and a wall clock beside it. The counter is derived
// from vertical blanks (the clock that paces the game loop), never from the wall clock; only fast-forward scales
// the wall clock.

namespace sh::timing {

// Starts both clocks from now.
void start_line_counter();
// H-blanks since the last start or clear (263 per vertical blank, NTSC).
int line_count();
void clear_line_count();
// Wall clock in Q12 seconds since the start; runs 4x faster while fast-forward is active.
long long wall_clock_q12();

}  // namespace sh::timing
