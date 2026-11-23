/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "engine/timing/pc_timing.h"

#include "engine/game_state/game_state.h"

namespace {
// One PSX game frame (1/30 s) in Q12 seconds: Q12(1.0f / 30.0f) truncates to 136.
constexpr int kTick30Q12 = 136;
}  // namespace

int PC_Tick30HzReady(int* accumulator_q12)
{
    *accumulator_q12 += sh::game_state::delta_time();
    if (*accumulator_q12 >= kTick30Q12)
    {
        *accumulator_q12 -= kTick30Q12;
        /* Clamp on huge stalls (alt-tab, paused breakpoint) so we don't
         * fire a burst of ticks afterwards. */
        if (*accumulator_q12 > kTick30Q12)
        {
            *accumulator_q12 = 0;
        }
        return 1;
    }
    return 0;
}
