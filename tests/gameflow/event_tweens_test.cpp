// Event tweens against their specification (the original's event helpers): clamped timers, truncating
// division, values worked out by hand.

#include "engine/gameflow/event_tweens.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

SH_TEST(event_tween_linear_clamps_at_duration) {
    gf::EventTweens t;
    // target 1000 over 4096 (1 s), steps of 136: 1000 * 136 / 4096 = 33 (truncated).
    SH_CHECK(t.linear(1000, 4096, 2, 136) == 33);
    SH_CHECK(t.timer(2) == 136);
    SH_CHECK(t.linear(1000, 4096, 2, 5000) == 1000);
    SH_CHECK(t.timer(2) == 4096);
    t.reset(2);
    SH_CHECK(t.timer(2) == 0);
    SH_CHECK(t.linear(-1000, 4096, 2, 136) == -33);  // truncation towards zero
}

SH_TEST(event_tween_sine_sweeps_angle) {
    gf::EventTweens t;
    // Quarter turn sweep over 1 s, full time: sin(0 + 1024) = 4096; amplitude 2048 -> 2048.
    SH_CHECK(t.sine(2048, 0, 1024, 4096, 0, 4096) == 2048);
    // Timers are independent.
    SH_CHECK(t.timer(1) == 0);
    SH_CHECK(t.sine(2048, 0, 1024, 4096, 1, 0) == 0);
}
