// SPDX-License-Identifier: GPL-3.0-or-later
// sh::menu::TouchScroll - a list that a finger scrolls the way a phone's lists scroll: the list follows the finger
// 1:1 while it drags, glides on with the finger's speed after it lifts and slows down, gives way only partly past
// either end (the rubber band) and springs back, and comes to rest on a whole row. Units are the list's own (menu
// lines), time is seconds. No renderer, no input layer: the view feeds it the finger and reads the offset.
#pragma once

#include <array>
#include <utility>
#include <vector>

namespace sh::menu {

class TouchScroll {
public:
    // The resting range: 0 .. max (the rows past the window, in units), and the row pitch a glide comes to rest on.
    void set_range(double max, double pitch);
    double max() const { return max_; }
    // Rows of their own heights: the offsets a glide comes to rest on (their tops) instead of every pitch; empty:
    // every pitch.
    void set_stops(std::vector<double> stops) { stops_ = std::move(stops); }

    // A finger went down on the list: a glide stops where it is (that touch only stops it).
    void touch();
    // The finger started to drag at y (units) at time t; until release() the offset follows it.
    void grab(double y, double t);
    // The dragging finger is at y at time t.
    void drag(double y, double t);
    // The finger lifted at time t: the glide starts with its speed over the last moments.
    void release(double t);
    // `seconds` passed: the glide slows down, an end overshot springs back, a slow list settles on a row.
    void update(double seconds);
    // The list jumps to `offset` (the keys moved the selection out of the window); clamped to the range.
    void jump(double offset);

    double offset() const { return offset_; }
    double velocity() const { return velocity_; }
    bool held() const { return held_; }
    // Gliding, springing back or settling: the offset still changes without a finger.
    bool moving() const { return !held_ && moving_; }
    // A finger down now would only stop the list: it moved fast enough that a tap must not pick a row.
    bool flinging() const { return moving() && (velocity_ > kStopTapSpeed || velocity_ < -kStopTapSpeed); }

    // The rubber band: `past` units beyond an end show as this much (never more than kBand).
    static double band(double past);

    static constexpr double kFriction = 3.0;        // 1/s: the glide's speed falls by e every third of a second
    static constexpr double kMaxSpeed = 2400.0;     // units/s
    static constexpr double kSettleSpeed = 60.0;    // units/s: slower than this the list settles on a row
    static constexpr double kStopTapSpeed = 120.0;  // units/s
    static constexpr double kBand = 48.0;           // units: the farthest the rubber band lets the list go
    static constexpr double kSpring = 14.0;         // 1/s: how fast an overshot end or a settling row is reached
    static constexpr double kSampleWindow = 0.10;   // s: the finger's speed is measured over its last moments

private:
    double clamped(double v) const;
    double nearest_row(double v) const;

    double max_ = 0.0;
    double pitch_ = 0.0;
    std::vector<double> stops_;
    double offset_ = 0.0;
    double velocity_ = 0.0;
    bool held_ = false;
    bool moving_ = false;

    // The drag: where the finger and the offset were when it began (the offset unbanded).
    double grab_y_ = 0.0;
    double grab_offset_ = 0.0;
    struct Sample {
        double y = 0.0, t = 0.0;
    };
    std::array<Sample, 8> samples_{};
    int sample_count_ = 0;
    int sample_next_ = 0;
};

}  // namespace sh::menu
