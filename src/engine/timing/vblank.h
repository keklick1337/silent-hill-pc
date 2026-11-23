// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The game's vertical blank: the count it reads, the waits of its frame loop, the interrupt callback it installs
// and the replay ("virtual") time built on it. A thin, process-wide front of sh::loop::VBlankClock that also
// applies the fast-forward rule: while fast-forward runs, every count read adds a blank and waits return at once.

#include <cstdint>

// Non-zero while fast-forward runs (the quick options toggle or the console's `ff`, mirrored by the main loop).
extern int g_skipSwapInterval;

namespace sh::timing {

inline constexpr int kBlankRate = 60;

// Blanks per second.
int blank_rate();

// Starts the real-time interrupt thread (nothing in virtual time). False when it cannot start.
bool start_vblank();
void stop_vblank();

// The blank count as the game sees it.
int vblank_count();
// Waits for the next blank (virtual time: raises it) and returns the count.
int wait_vblank();
// The game's wait for `count` blanks: 0 waits for the next one like wait_vblank(); a positive count only reads the
// count, since the frame pacer does the longer waits of the console. Returns the count.
int wait_vblanks(int count);
// The game's per-blank interrupt handler; nullptr removes it.
void set_vblank_callback(void (*callback)());

// Replay mode: on when SH_REPLAY_INPUT or SH_REPLAY_RECORD is set. The blank count then
// advances only through a wait for a blank, the interrupt thread does not run and audio is rendered per blank, so a
// run depends on its input alone, not on the machine or the wall clock. Read from the environment on first use.
namespace virtual_time {

bool active();
// SH_REPLAY_RECORD or SH_REPLAY_PACED: a wait also sleeps up to the blank's wall-clock deadline, so a person can
// play or watch. Never changes the count.
bool paced();
// SDL_GetTicks() outside replay mode; the blank count in milliseconds inside it. For the few logic timers kept in
// milliseconds.
std::uint32_t ticks_ms();
// One blank now: the callback, the count, and the audio of that blank.
void step_blank();
// A device poll that found the device not ready. On the console the drive keeps working while the CPU spins on
// it; here nothing else advances the count, so after 64 such polls without a blank one blank passes.
void device_busy();

}  // namespace virtual_time

}  // namespace sh::timing
