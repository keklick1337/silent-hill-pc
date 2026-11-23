#pragma once

// Event tweens: map event scripts animate values over time with
// a few shared timers. Each call advances its timer by the frame time, clamps it at the duration and
// returns the value at that point: linear from 0 to a target, or a sine swept between two angles.
// Integer arithmetic as in the game (32-bit products, truncating division).

#include <array>
#include <cstdint>

namespace sh::gameflow {

class EventTweens {
public:
    static constexpr int kTimers = 6;

    void reset(int timer);

    // `target * t / duration`, t = the timer after advancing by `delta` (Q12 seconds) and clamping.
    std::int32_t linear(std::int32_t target, std::int32_t duration, int timer, std::int32_t delta);

    // `amplitude * sin(start + sweep * t / duration)`, angles in Q12 (4096 per turn).
    std::int32_t sine(std::int32_t amplitude, std::int16_t start, std::int16_t sweep, std::int32_t duration, int timer,
                      std::int32_t delta);

    std::int32_t timer(int timer) const { return timers_[static_cast<std::size_t>(timer)]; }

private:
    std::int32_t advance(int timer, std::int32_t duration, std::int32_t delta);

    std::array<std::int32_t, kTimers> timers_{};
};

}  // namespace sh::gameflow
