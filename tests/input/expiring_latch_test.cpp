// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/input/expiring_latch.h"

using sh::input::ExpiringLatch;

SH_TEST(latch_press_arms_for_the_window) {
    ExpiringLatch l;
    SH_CHECK(!l.active());
    // Press with conditions: armed.
    l.sample(true, true, 136, 1228);
    SH_CHECK(l.active());
    // Held (no new edge) with conditions still true: the window still counts down.
    l.sample(false, true, 136, 1228);
    SH_CHECK(l.active());
    SH_CHECK(l.remaining_q12() == 1228 - 136);
}

SH_TEST(latch_expires_without_a_press) {
    ExpiringLatch l;
    l.sample(true, true, 136, 1228);
    for (int i = 0; i < 9; i++) l.sample(false, false, 136, 1228);
    SH_CHECK(l.active());  // 9 x 136 = 1224 < 1228: one frame of window left
    l.sample(false, false, 136, 1228);
    SH_CHECK(!l.active());  // the 10th frame crosses zero
    SH_CHECK(l.remaining_q12() <= 0);
}

SH_TEST(latch_press_without_conditions_is_ignored) {
    ExpiringLatch l;
    l.sample(true, false, 136, 1228);
    SH_CHECK(!l.active());
    // A press while already armed but with conditions false does NOT refresh
    // the window: the countdown branch runs.
    l.sample(true, true, 136, 1228);
    l.sample(true, false, 136, 1228);
    SH_CHECK(l.active());
    SH_CHECK(l.remaining_q12() == 1228 - 136);
}

SH_TEST(latch_raw_writes_keep_the_port_global_semantics) {
    ExpiringLatch l;
    // An external set arms without a countdown: one frame of dt drops it.
    l.set_active(true);
    SH_CHECK(l.active());
    SH_CHECK(l.remaining_q12() == 0);
    l.sample(false, false, 136, 1228);
    SH_CHECK(!l.active());

    // An external clear stops the countdown; a later press re-arms normally.
    l.sample(true, true, 136, 1228);
    l.set_active(false);
    SH_CHECK(!l.active());
    l.sample(false, true, 136, 1228);  // nothing left to count down
    SH_CHECK(!l.active());

    l.clear();
    SH_CHECK(!l.active());
    SH_CHECK(l.remaining_q12() == 0);
}
