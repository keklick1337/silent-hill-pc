// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ring a finger turns (input/ring_carousel.h) and the inventory pointer driving the screen's ring with it
// (input/inventory_pointer.h).
#include "input/ring_carousel.h"

#include "framework/sh_test.h"
#include "input/inventory_pointer.h"

#include <cmath>

using sh::input::InventoryPointer;
using sh::input::RingCarousel;
using Press = InventoryPointer::Press;

namespace {

constexpr double kPitch = 62.0;
constexpr double kFrame = 1.0 / 60.0;

bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

RingCarousel ring(int count, int item) {
    RingCarousel r;
    r.set_count(count);
    r.set_pitch(kPitch);
    r.jump(item);
    return r;
}

// Runs the ring until it rests; returns the ticks it made on the way.
int settle(RingCarousel& r) {
    int ticks = 0;
    for (int i = 0; i < 2000 && r.active(); ++i) {
        r.update(kFrame);
        ticks += r.take_ticks();
    }
    return ticks;
}

// A finger dragging from 0 by `step` pixels a frame, `frames` times; returns the ticks.
int drag_steadily(RingCarousel& r, double step, int frames) {
    r.press(0.0);
    int ticks = 0;
    for (int i = 1; i <= frames; ++i) {
        r.drag(step * i, kFrame);
        ticks += r.take_ticks();
    }
    return ticks;
}

}  // namespace

SH_TEST(ring_carousel_follows_the_finger) {
    RingCarousel r = ring(10, 4);
    r.press(100.0);
    SH_CHECK(near(r.position(), 4.0));
    r.drag(100.0 - kPitch, kFrame);  // left by one item: the next item comes to the centre
    SH_CHECK(near(r.position(), 5.0) && r.item() == 5);
    r.drag(100.0 + kPitch / 2.0, kFrame);
    SH_CHECK(near(r.position(), 3.5));
    r.drag(100.0 + kPitch * 0.25, kFrame);
    SH_CHECK(near(r.position(), 3.75) && r.item() == 4);
    SH_CHECK(r.held() && r.grabbed() && !r.moving());
}

SH_TEST(ring_carousel_fling_glides_on_and_slows_down) {
    RingCarousel r = ring(20, 2);
    int ticks = drag_steadily(r, -10.0, 12);  // 600 px/s to the left: about ten items a second
    const double let_go = r.position();
    r.release();
    SH_CHECK(r.moving() && r.velocity() > 8.0 && r.flinging());
    const double thrown = r.velocity();
    for (int i = 0; i < 10; ++i) {
        r.update(kFrame);
        ticks += r.take_ticks();
    }
    SH_CHECK(r.velocity() > 0.0 && r.velocity() < thrown);
    SH_CHECK(r.position() > let_go);
    ticks += settle(r);
    SH_CHECK(!r.active());
    SH_CHECK(r.position() > let_go + 1.0);  // further than the drag took it
    SH_CHECK(near(r.position(), std::floor(r.position())));
    SH_CHECK(ticks == r.item() - 2);  // one tick for each item passed
}

SH_TEST(ring_carousel_finger_that_rests_throws_nothing) {
    RingCarousel r = ring(20, 2);
    drag_steadily(r, -10.0, 12);
    const double x = -120.0;
    for (int i = 0; i < 12; ++i) r.drag(x, kFrame);  // holds still for a fifth of a second
    r.release();
    SH_CHECK(near(r.velocity(), 0.0));
    settle(r);
    SH_CHECK(r.item() == 4 && near(r.position(), 4.0));  // 2 + 120/62 = 3.94: the nearest item
}

SH_TEST(ring_carousel_snaps_to_the_nearest_item_either_side) {
    const double shares[] = {0.3, 0.7, -0.4, -0.6};
    const int expect[] = {4, 5, 4, 3};
    for (int i = 0; i < 4; ++i) {
        RingCarousel r = ring(10, 4);
        r.press(0.0);
        r.drag(-shares[i] * kPitch, kFrame);
        for (int k = 0; k < 10; ++k) r.drag(-shares[i] * kPitch, kFrame);
        r.release();
        settle(r);
        SH_CHECK(r.item() == expect[i] && near(r.position(), expect[i]));
    }
}

SH_TEST(ring_carousel_ticks_once_per_item_passed) {
    RingCarousel r = ring(20, 10);
    SH_CHECK(drag_steadily(r, -kPitch / 4.0, 12) == 3);  // three items to the left
    SH_CHECK(r.item() == 13);
    r.drag(0.0, kFrame);  // back to where the finger landed
    SH_CHECK(r.take_ticks() == -3 && r.item() == 10);

    // A slow release just past half an item must not swing back and pass it a second time.
    r = ring(20, 10);
    drag_steadily(r, -kPitch * 0.45 / 20.0, 20);
    r.release();
    const int at_release = r.item();
    const int ticks = settle(r);
    SH_CHECK(ticks == r.item() - at_release && ticks >= -1 && ticks <= 1);
}

SH_TEST(ring_carousel_wraps_round) {
    RingCarousel r = ring(8, 0);
    r.press(0.0);
    r.drag(2.0 * kPitch, kFrame);  // right by two items: the last ones come round
    SH_CHECK(r.item() == 6 && near(r.position(), 6.0) && r.take_ticks() == -2);
    r.drag(-3.0 * kPitch, kFrame);
    SH_CHECK(r.item() == 3 && near(r.position(), 3.0) && r.take_ticks() == 5);
    for (int i = 0; i < 12; ++i) r.drag(-3.0 * kPitch, kFrame);  // rests before it lifts
    r.release();
    settle(r);
    SH_CHECK(r.item() == 3);
}

SH_TEST(ring_carousel_turns_softly_to_a_neighbour) {
    RingCarousel r = ring(10, 1);
    r.turn_by(-2);
    SH_CHECK(r.moving() && !r.flinging());
    r.update(kFrame);
    SH_CHECK(r.position() < 1.0 || r.position() > 9.0);  // on its way, not there at once
    const int ticks = r.take_ticks() + settle(r);
    SH_CHECK(r.item() == 9 && near(r.position(), 9.0) && ticks == -2);
}

SH_TEST(ring_carousel_landing_finger_stops_a_glide) {
    RingCarousel r = ring(20, 2);
    drag_steadily(r, -10.0, 12);
    r.release();
    r.update(kFrame);
    r.touch();
    const double stopped = r.position();
    r.update(kFrame);
    SH_CHECK(r.held() && !r.grabbed() && near(r.position(), stopped));
    r.release();
    settle(r);
    SH_CHECK(near(r.position(), std::floor(stopped + 0.5)));
}

namespace {

InventoryPointer::View touch_at(int x, int y, int selected) {
    InventoryPointer::View v;
    v.x = x;
    v.y = y;
    v.item_count = 10;
    v.selected_item = selected;
    v.touch = true;
    v.seconds = kFrame;
    return v;
}

}  // namespace

SH_TEST(inventory_pointer_finger_drags_the_ring) {
    InventoryPointer p;
    InventoryPointer::View v = touch_at(0, 0, 4);
    v.finger_down = true;
    SH_CHECK(!p.update(v).ring_position.has_value());  // a finger that only lands takes nothing yet

    v.x = -70;  // dragged left past one item
    v.finger_dragging = v.finger_horizontal = true;
    InventoryPointer::Action a = p.update(v);
    SH_CHECK(a.ring_position && near(*a.ring_position, 4.0 + 70.0 / kPitch));
    SH_CHECK(a.press == Press::RingRight && a.settle);

    v.selected_item = 5;  // the screen stepped after it
    a = p.update(v);
    SH_CHECK(a.ring_position && a.press == Press::None);

    v.finger_down = v.finger_dragging = v.finger_horizontal = false;  // lifted after resting
    for (int i = 0; i < 200 && a.ring_position; ++i) a = p.update(v);
    SH_CHECK(!a.ring_position && a.press == Press::None);  // at rest on item 5: the screen has the ring again

    // A drag that ends on the ring turns nothing when the pad is used after it.
    v.selected_item = 6;
    a = p.update(v);
    SH_CHECK(!a.ring_position && a.press == Press::None);
}

SH_TEST(inventory_pointer_tap_turns_to_a_neighbour_and_opens_the_centre) {
    InventoryPointer p;
    InventoryPointer::View v = touch_at(60, 0, 4);  // the right neighbour
    v.finger_down = true;
    p.update(v);
    v.finger_down = false;  // lifted: the tap clicks
    v.clicked = true;
    InventoryPointer::Action a = p.update(v);
    SH_CHECK(a.ring_position.has_value() && a.press == Press::None);
    v.clicked = false;
    bool stepped = false;
    for (int i = 0; i < 200 && a.ring_position; ++i) {
        a = p.update(v);
        if (a.press == Press::RingRight) {
            stepped = true;
            v.selected_item = 5;
        }
    }
    SH_CHECK(stepped && !a.ring_position);

    v = touch_at(0, 0, 5);  // the centre item
    v.finger_down = true;
    p.update(v);
    v.finger_down = false;
    v.clicked = true;
    a = p.update(v);
    SH_CHECK(a.press == Press::Enter && !a.ring_position);

    // A mouse click on a neighbour still walks the ring there one press at a time.
    v = touch_at(60, 0, 5);
    for (int i = 0; i < 5; ++i) p.update(v);
    v.clicked = true;
    a = p.update(v);
    v.clicked = false;
    a = p.update(v);
    SH_CHECK(!a.ring_position && a.press == Press::RingRight);
}
