// Presses on frames presented between two game updates (engine/loop/pad_tracker.h
// section 2.1): a button that went down and up again before the next update still reaches it, once.

#include "engine/loop/pad_tracker.h"

#include "framework/sh_test.h"

namespace lp = sh::loop;

SH_TEST(loop_button_latch_keeps_a_press_for_the_next_update) {
    lp::ButtonLatch latch;
    constexpr std::uint16_t kNone = 0xFFFF;
    constexpr std::uint16_t kCross = 1u << 14;
    // Nothing seen: the update reads its own sample.
    SH_CHECK(latch.take(kNone) == kNone);
    // Down on one presented frame, up again on the next and at the update.
    latch.seen(static_cast<std::uint16_t>(~kCross));
    latch.seen(kNone);
    SH_CHECK(latch.take(kNone) == static_cast<std::uint16_t>(~kCross));
    // Once: the update after it sees the button up.
    SH_CHECK(latch.take(kNone) == kNone);
    // A held button reads the same with or without the latch.
    latch.seen(static_cast<std::uint16_t>(~kCross));
    SH_CHECK(latch.take(static_cast<std::uint16_t>(~kCross)) == static_cast<std::uint16_t>(~kCross));
}
