// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A latched one-bit request that expires on its own (the manual-reload
// request). Sampled every frame:
// a press while the conditions hold arms the latch for a short window; without
// a new press the window counts down and the request drops. The window is short
// on purpose — a stale press resurfacing later is exactly the spurious action
// the latch exists to remove, so it must not live long.
//
// The consumer side (a state machine, a script variable) may read the flag and
// write it: set_active() writes the flag without touching the countdown (a set
// from outside rides on whatever countdown is left; a clear stops the
// countdown).

#pragma once

#include <cstdint>

namespace sh::input {

class ExpiringLatch {
public:
    // One frame. `press` is the bind's rising edge this frame, `conditions` the
    // "may act right now" test evaluated at press time, `dt_q12` the frame
    // delta (Q12 seconds), `window_q12` how long a press stays latched.
    void sample(bool press, bool conditions, std::int32_t dt_q12, std::int32_t window_q12) {
        if (press && conditions) {
            active_ = true;
            t_q12_ = window_q12;
        } else if (active_) {
            t_q12_ -= dt_q12;
            if (t_q12_ <= 0) active_ = false;
        }
    }

    bool active() const { return active_; }

    // Raw flag write, as a plain flag would be: does not touch the countdown.
    void set_active(bool active) { active_ = active; }

    void clear() {
        active_ = false;
        t_q12_ = 0;
    }

    std::int32_t remaining_q12() const { return t_q12_; }

private:
    bool active_ = false;
    std::int32_t t_q12_ = 0;
};

}  // namespace sh::input
