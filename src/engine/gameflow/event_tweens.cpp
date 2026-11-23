#include "engine/gameflow/event_tweens.h"

#include "engine/world/fixed_trig.h"

namespace sh::gameflow {

namespace {

constexpr std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

}  // namespace

void EventTweens::reset(int timer) {
    timers_[static_cast<std::size_t>(timer)] = 0;
}

std::int32_t EventTweens::advance(int timer, std::int32_t duration, std::int32_t delta) {
    std::int32_t& t = timers_[static_cast<std::size_t>(timer)];
    t += delta;
    if (duration < t) {
        t = duration;
    }
    return t;
}

std::int32_t EventTweens::linear(std::int32_t target, std::int32_t duration, int timer, std::int32_t delta) {
    const std::int32_t t = advance(timer, duration, delta);
    return wrap_mul(target, t) / duration;
}

std::int32_t EventTweens::sine(std::int32_t amplitude, std::int16_t start, std::int16_t sweep, std::int32_t duration,
                               int timer, std::int32_t delta) {
    const std::int32_t t = advance(timer, duration, delta);
    const std::int32_t angle = start + wrap_mul(sweep, t) / duration;
    return wrap_mul(amplitude, sh::world::fixed_sin(angle)) >> 12;
}

}  // namespace sh::gameflow
