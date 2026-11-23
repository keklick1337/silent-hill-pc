/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* One step of a scripted-cutscene clock: the value runs inside a window [start, end] of the
 * cutscene timeline at `rate` timeline frames per second. Shared by every map; the exe exports
 * the C entry used by map code and Lua. */

#include <cstdint>

namespace sh::maps {

struct CutsceneClockStep {
    std::int32_t value = 0;
    bool reached_end = false;  // the window end was passed this step
};

// All times are Q12; `delta` is this frame's Q12 delta time. A clock still before the window
// jumps to its start and does not run this frame. Past the end it is held at `end` only when
// `hold_at_end` is set, so a clock without it keeps running beyond the window.
constexpr CutsceneClockStep cutscene_clock_step(std::int32_t value, std::int32_t delta, std::int32_t rate,
                                                std::int32_t start, std::int32_t end, bool hold_at_end) {
    if (value < start) {
        return {start, false};
    }
    // 64-bit product, then an arithmetic shift back to Q12 (floor, also for negative products).
    const std::int64_t product = static_cast<std::int64_t>(delta) * static_cast<std::int64_t>(rate);
    value += static_cast<std::int32_t>(product >> 12);
    if (value > end) {
        return {hold_at_end ? end : value, true};
    }
    return {value, false};
}

}  // namespace sh::maps
