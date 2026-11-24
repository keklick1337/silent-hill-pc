// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/menu/touch_scroll.h"

#include <algorithm>
#include <cmath>

namespace sh::menu {

namespace {

constexpr double kRestEpsilon = 0.25;  // units: close enough to the resting place to stop there

}  // namespace

double TouchScroll::band(double past) {
    if (past <= 0.0) return 0.0;
    // Gives way less and less the farther the finger pulls: half of the pull at first, never more than kBand.
    return past * kBand / (past + 2.0 * kBand);
}

// The pull that band() shows as `shown`.
static double unband(double shown) {
    shown = std::clamp(shown, 0.0, TouchScroll::kBand * 0.999);
    return 2.0 * TouchScroll::kBand * shown / (TouchScroll::kBand - shown);
}

double TouchScroll::clamped(double v) const { return std::clamp(v, 0.0, max_); }

double TouchScroll::nearest_row(double v) const {
    if (!stops_.empty()) {
        double best = clamped(stops_.front());
        for (const double stop : stops_) {
            const double c = clamped(stop);
            if (std::fabs(c - v) < std::fabs(best - v)) best = c;
        }
        return best;
    }
    if (pitch_ <= 0.0) return clamped(v);
    return clamped(std::round(v / pitch_) * pitch_);
}

void TouchScroll::set_range(double max, double pitch) {
    max_ = std::max(0.0, max);
    pitch_ = std::max(0.0, pitch);
    // Rows that went away leave the list past its end: it springs back.
    if (!held_ && (offset_ < 0.0 || offset_ > max_)) moving_ = true;
}

void TouchScroll::touch() {
    velocity_ = 0.0;
}

void TouchScroll::grab(double y, double t) {
    held_ = true;
    moving_ = false;
    velocity_ = 0.0;
    grab_y_ = y;
    // Grabbed past an end (springing back): the finger takes the list from where it shows.
    grab_offset_ = offset_;
    if (offset_ < 0.0) grab_offset_ = -unband(-offset_);
    if (offset_ > max_) grab_offset_ = max_ + unband(offset_ - max_);
    sample_count_ = 0;
    sample_next_ = 0;
    drag(y, t);
}

void TouchScroll::drag(double y, double t) {
    if (!held_) return;
    // The finger moving up moves the list up: later rows come into view.
    const double raw = grab_offset_ + (grab_y_ - y);
    if (raw < 0.0) {
        offset_ = -band(-raw);
    } else if (raw > max_) {
        offset_ = max_ + band(raw - max_);
    } else {
        offset_ = raw;
    }
    samples_[static_cast<std::size_t>(sample_next_)] = {y, t};
    sample_next_ = (sample_next_ + 1) % static_cast<int>(samples_.size());
    sample_count_ = std::min(sample_count_ + 1, static_cast<int>(samples_.size()));
}

void TouchScroll::release(double t) {
    if (!held_) return;
    held_ = false;
    moving_ = true;
    velocity_ = 0.0;
    if (sample_count_ < 2) return;
    const int n = static_cast<int>(samples_.size());
    const Sample& last = samples_[static_cast<std::size_t>((sample_next_ + n - 1) % n)];
    // A finger that rested before it lifted throws nothing.
    if (t - last.t > kSampleWindow) return;
    const Sample* first = &last;
    for (int i = 2; i <= sample_count_; ++i) {
        const Sample& s = samples_[static_cast<std::size_t>((sample_next_ + n - i) % n)];
        if (last.t - s.t > kSampleWindow) break;
        first = &s;
    }
    const double dt = last.t - first->t;
    if (dt <= 1e-4) return;
    velocity_ = std::clamp(-(last.y - first->y) / dt, -kMaxSpeed, kMaxSpeed);
}

void TouchScroll::jump(double offset) {
    if (held_) return;
    offset_ = clamped(offset);
    velocity_ = 0.0;
    moving_ = false;
}

void TouchScroll::update(double seconds) {
    if (held_ || !moving_ || seconds <= 0.0) return;
    const double spring = 1.0 - std::exp(-kSpring * seconds);
    if (offset_ < 0.0 || offset_ > max_) {
        // Past an end: the glide is caught by the band and the list springs back to the end.
        const double end = clamped(offset_);
        offset_ += velocity_ * seconds;
        velocity_ *= std::exp(-8.0 * kSpring * seconds);
        offset_ = std::clamp(offset_, -kBand, max_ + kBand);
        offset_ += (end - offset_) * spring;
        if (std::fabs(offset_ - end) < kRestEpsilon && std::fabs(velocity_) < kSettleSpeed) {
            offset_ = end;
            velocity_ = 0.0;
            moving_ = false;
        }
        return;
    }
    if (std::fabs(velocity_) >= kSettleSpeed) {
        offset_ += velocity_ * seconds;
        velocity_ *= std::exp(-kFriction * seconds);
        return;
    }
    // Slow enough: the list settles on the nearest whole row.
    velocity_ = 0.0;
    const double target = nearest_row(offset_);
    offset_ += (target - offset_) * spring;
    if (std::fabs(offset_ - target) < kRestEpsilon) {
        offset_ = target;
        moving_ = false;
    }
}

}  // namespace sh::menu
