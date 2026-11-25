// SPDX-License-Identifier: GPL-3.0-or-later
//
// sh::input::RingCarousel - a ring of items that a finger turns the way a phone's carousels turn: the ring follows
// the finger 1:1 while it drags, glides on with the finger's speed after it lifts and slows down, and comes softly
// to rest on the item nearest the centre, whichever side that is. Positions are in items (fractional while the ring
// moves), the finger in the caller's pixels, time in seconds. No SDL, no game: the caller feeds the finger and
// reads the position.
#pragma once

#include <array>

namespace sh::input {

class RingCarousel {
public:
    // Places in the ring (positions wrap at it) and how many pixels the finger moves the ring by one item.
    void set_count(int count);
    void set_pitch(double pixels_per_item);
    int count() const { return count_; }

    // At rest on `item`: the ring is where the screen keeps it. Ignored while a finger holds the ring.
    void jump(int item);

    // A finger landed on the ring: a glide stops where it is, and the ring waits for the finger to drag or lift.
    void touch();
    // The finger starts to drag at x; until release() the ring follows it. A finger moving right brings the items
    // on the left to the centre (the position falls).
    void press(double x);
    // The dragging finger is at x, `seconds` after the last call.
    void drag(double x, double seconds);
    // The finger lifted: the ring glides on with the finger's speed over its last moments, then settles.
    void release();
    // A soft turn by `places` items from the item nearest the centre (a tap on a neighbour).
    void turn_by(int places);
    // `seconds` passed: a glide slows down, a slow ring settles on the nearest item.
    void update(double seconds);

    // 0 .. count: the item at the centre, with the fraction it has moved on.
    double position() const;
    // The item nearest the centre, 0 .. count - 1.
    int item() const;
    // Items the centre passed since the last call (positive: towards higher items). An item is passed when it
    // stops being the nearest one, so each counts once for each time the ring crosses it.
    int take_ticks();

    double velocity() const { return velocity_; }
    bool held() const { return held_; }
    // A finger has dragged since it landed (touch() alone does not grab).
    bool grabbed() const { return held_ && grabbed_; }
    // Gliding or settling: the position still changes without a finger.
    bool moving() const { return !held_ && moving_; }
    bool active() const { return held_ || moving_; }
    // A finger down now should only stop the ring: it moves too fast for a tap to pick an item.
    bool flinging() const { return moving() && (velocity_ > kStopTapSpeed || velocity_ < -kStopTapSpeed); }

    static constexpr double kFriction = 4.0;       // 1/s: a glide's speed falls by e every quarter second
    static constexpr double kMaxSpeed = 24.0;      // items/s
    static constexpr double kSettleSpeed = 2.0;    // items/s: slower than this the ring settles on an item
    static constexpr double kStopTapSpeed = 3.0;   // items/s
    static constexpr double kSpring = 14.0;        // 1/s: how fast a settling ring reaches its item
    static constexpr double kSampleWindow = 0.10;  // s: the finger's speed is measured over its last moments

private:
    void moved();

    int count_ = 0;
    double pitch_ = 1.0;
    double position_ = 0.0;  // unwrapped
    double velocity_ = 0.0;  // items/s
    double target_ = 0.0;    // the item a settling ring comes to rest on (unwrapped)
    bool settling_ = false;
    bool held_ = false;
    bool grabbed_ = false;
    bool moving_ = false;
    long long nearest_ = 0;  // unwrapped item nearest the centre, for the ticks
    int ticks_ = 0;

    // The drag: where the finger and the ring were when it began, and the finger's last positions.
    double grab_x_ = 0.0;
    double grab_position_ = 0.0;
    double clock_ = 0.0;
    struct Sample {
        double x = 0.0, t = 0.0;
    };
    std::array<Sample, 16> samples_{};
    int sample_count_ = 0;
    int sample_next_ = 0;
};

}  // namespace sh::input
