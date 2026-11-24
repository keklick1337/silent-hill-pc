// SPDX-License-Identifier: GPL-3.0-or-later
// A list scrolled by a finger: 1:1 while it drags, the glide after it lifts, the rubber band at the ends, the rest
// on a whole row.

#include <cmath>

#include "engine/menu/touch_scroll.h"
#include "framework/sh_test.h"

using sh::menu::TouchScroll;

namespace {

// Runs the list for `seconds` in frames of 1/60 s.
void run(TouchScroll& s, double seconds) {
    for (double t = 0.0; t < seconds; t += 1.0 / 60.0) s.update(1.0 / 60.0);
}

bool near(double a, double b, double eps = 0.01) { return std::fabs(a - b) <= eps; }

}  // namespace

SH_TEST(touch_scroll_follows_the_finger_one_to_one) {
    TouchScroll s;
    s.set_range(240.0, 24.0);
    s.grab(100.0, 0.0);
    SH_CHECK(s.held() && near(s.offset(), 0.0));
    s.drag(70.0, 0.1);  // the finger moves up 30 lines: the list shows 30 lines further
    SH_CHECK(near(s.offset(), 30.0));
    s.drag(110.0, 0.2);  // and down past the start: the band gives way only partly
    SH_CHECK(s.offset() < 0.0 && s.offset() > -10.0);
    s.drag(-1000.0, 0.3);  // far past the end: never more than the band
    SH_CHECK(s.offset() > 240.0 && s.offset() <= 240.0 + TouchScroll::kBand);
}

SH_TEST(touch_scroll_rests_on_a_row_and_springs_back) {
    TouchScroll s;
    s.set_range(240.0, 24.0);
    // A slow drag that stops: no glide, the list settles on the nearest row.
    s.grab(100.0, 0.0);
    s.drag(90.0, 0.5);
    s.drag(85.0, 1.0);
    s.release(1.5);  // rested half a second before lifting
    SH_CHECK(near(s.velocity(), 0.0));
    run(s, 2.0);
    SH_CHECK(!s.moving() && near(s.offset(), 24.0));
    // Pulled past the start and let go: back to 0.
    s.grab(100.0, 2.0);
    s.drag(200.0, 2.5);
    SH_CHECK(s.offset() < 0.0);
    s.release(3.5);
    run(s, 2.0);
    SH_CHECK(!s.moving() && near(s.offset(), 0.0));
}

SH_TEST(touch_scroll_glides_with_the_flick) {
    TouchScroll s;
    s.set_range(2400.0, 24.0);
    // A flick upwards: 120 lines in 0.1 s.
    s.grab(200.0, 0.0);
    for (int i = 1; i <= 6; ++i) s.drag(200.0 - 20.0 * i, i / 60.0);
    s.release(6.0 / 60.0 + 0.01);
    SH_CHECK(s.velocity() > 1000.0 && s.moving() && s.flinging());
    const double lifted = s.offset();
    run(s, 0.2);
    SH_CHECK(s.offset() > lifted + 100.0);
    // It slows down and stops on a whole row.
    run(s, 5.0);
    SH_CHECK(!s.moving() && s.offset() > lifted + 200.0);
    SH_CHECK(near(std::fmod(s.offset(), 24.0), 0.0));
    // A finger that lands on a glide stops it.
    s.grab(100.0, 10.0);
    for (int i = 1; i <= 6; ++i) s.drag(100.0 + 20.0 * i, 10.0 + i / 60.0);
    s.release(10.0 + 6.0 / 60.0);
    SH_CHECK(s.velocity() < -1000.0);
    run(s, 0.05);
    s.touch();
    SH_CHECK(near(s.velocity(), 0.0) && !s.flinging());
}

SH_TEST(touch_scroll_glide_stops_at_the_end) {
    TouchScroll s;
    s.set_range(48.0, 24.0);
    s.grab(200.0, 0.0);
    for (int i = 1; i <= 6; ++i) s.drag(200.0 - 10.0 * i, i / 60.0);
    s.release(0.1);
    run(s, 3.0);
    SH_CHECK(!s.moving() && near(s.offset(), 48.0));
    // Rows that went away while the list showed the end: it springs back into the shorter range.
    s.set_range(24.0, 24.0);
    SH_CHECK(s.moving());
    run(s, 2.0);
    SH_CHECK(near(s.offset(), 24.0));
    // The keys jump it (clamped).
    s.jump(1000.0);
    SH_CHECK(near(s.offset(), 24.0) && !s.moving());
}
