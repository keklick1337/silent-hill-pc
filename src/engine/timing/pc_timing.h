/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* Accumulator-based 30Hz tick check.
 *
 * Pass a per-call-site static accumulator (initialized to 0). Adds the
 * current frame's game delta time each call. Returns 1 (and decrements the
 * accumulator) when one full PSX game frame (1/30s) of game time has
 * elapsed; otherwise returns 0. The result is int, not bool: callers inside
 * a C ABI region and the engine must agree on its size.
 *
 * Why: PSX game logic ran at fixed 30Hz. Frame-counter state machines in
 * the game code (attack shift register, multi-tap detectors, etc.) assume
 * each call equals 1/30s. At PC framerates of 60-240Hz, those state
 * machines fire too quickly — the 4-frame attack hold detector triggers
 * in 17ms at 240fps instead of 133ms at 30fps, so even a quick tap
 * registers as a "hold". Wrap each such state machine in a guard like:
 *
 *     static int s_accum = 0;
 *     if (PC_Tick30HzReady(&s_accum)) { ... shift register ... }
 *
 * so it advances at the original PSX cadence regardless of render rate. */
#ifdef __cplusplus
extern "C" {
#endif

int PC_Tick30HzReady(int* accumulator_q12);

#ifdef __cplusplus
}
#endif
