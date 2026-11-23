// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/input/input_primitives.h"

namespace sh::input {

bool tick30_ready(std::int32_t& accumulator_q12, std::int32_t delta_q12) {
    accumulator_q12 += delta_q12;
    if (accumulator_q12 < kTick30Q12) return false;
    accumulator_q12 -= kTick30Q12;
    // A long stall (alt-tab, breakpoint) must not replay a burst of ticks afterwards.
    if (accumulator_q12 > kTick30Q12) accumulator_q12 = 0;
    return true;
}

}  // namespace sh::input
