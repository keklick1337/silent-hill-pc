// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/ring_carousel.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sh::input {

namespace {

constexpr double kRestDistance = 1e-3;  // items: close enough to the resting item to stop there
constexpr double kRestSpeed = 1e-2;     // items/s

}  // namespace

void RingCarousel::set_count(int count) { count_ = std::max(0, count); }

void RingCarousel::set_pitch(double pixels_per_item) { pitch_ = std::max(1e-3, pixels_per_item); }

void RingCarousel::jump(int item) {
    if (held_) return;
    position_ = target_ = item;
    velocity_ = 0.0;
    moving_ = settling_ = false;
    nearest_ = item;
    ticks_ = 0;
}

void RingCarousel::touch() {
    held_ = true;
    grabbed_ = false;
    moving_ = settling_ = false;
    velocity_ = 0.0;
}

void RingCarousel::press(double x) {
    held_ = grabbed_ = true;
    moving_ = settling_ = false;
    velocity_ = 0.0;
    grab_x_ = x;
    grab_position_ = position_;
    // The speed is measured from the first drag on: the way to the press (the distance a drag needs to start)
    // took the finger longer than one frame.
    sample_count_ = 0;
    sample_next_ = 0;
}

void RingCarousel::drag(double x, double seconds) {
    if (!grabbed()) return;
    clock_ += std::max(0.0, seconds);
    position_ = grab_position_ - (x - grab_x_) / pitch_;
    moved();
    samples_[static_cast<std::size_t>(sample_next_)] = {x, clock_};
    sample_next_ = (sample_next_ + 1) % static_cast<int>(samples_.size());
    sample_count_ = std::min(sample_count_ + 1, static_cast<int>(samples_.size()));
}

void RingCarousel::release() {
    if (!held_) return;
    const bool thrown = grabbed_;
    held_ = grabbed_ = false;
    moving_ = true;
    settling_ = false;
    velocity_ = 0.0;
    if (!thrown || sample_count_ < 2) return;
    // The finger's speed over its last moments: a finger that rested before it lifted throws nothing, and one
    // jittery frame does not decide the throw.
    const int n = static_cast<int>(samples_.size());
    const Sample& last = samples_[static_cast<std::size_t>((sample_next_ + n - 1) % n)];
    const Sample* first = &last;
    for (int i = 2; i <= sample_count_; ++i) {
        const Sample& s = samples_[static_cast<std::size_t>((sample_next_ + n - i) % n)];
        if (last.t - s.t > kSampleWindow) break;
        first = &s;
    }
    const double dt = last.t - first->t;
    if (dt <= 1e-4) return;
    velocity_ = std::clamp(-(last.x - first->x) / dt / pitch_, -kMaxSpeed, kMaxSpeed);
}

void RingCarousel::turn_by(int places) {
    if (held_ || count_ <= 0) return;
    target_ = static_cast<double>(nearest_ + places);
    settling_ = moving_ = true;
}

void RingCarousel::update(double seconds) {
    if (held_ || !moving_ || seconds <= 0.0) return;
    if (!settling_) {
        if (std::fabs(velocity_) >= kSettleSpeed) {
            // The glide over the step exactly, so a slow frame carries the ring as far as fast ones would.
            const double decay = std::exp(-kFriction * seconds);
            position_ += velocity_ * (1.0 - decay) / kFriction;
            velocity_ *= decay;
            moved();
            if (std::fabs(velocity_) >= kSettleSpeed) return;
        }
        settling_ = true;
        // The item nearest where the spring would carry the remaining speed: with the plain nearest item, a ring
        // that crosses a half item slowly could swing just past it and pass the same item twice.
        target_ = std::floor(position_ + velocity_ / kSpring + 0.5);
    }
    // A critically damped spring, solved exactly: it keeps the glide's speed and reaches the item without
    // swinging past it and back.
    const double e = position_ - target_;
    const double b = velocity_ + kSpring * e;
    const double decay = std::exp(-kSpring * seconds);
    position_ = target_ + (e + b * seconds) * decay;
    velocity_ = (b - kSpring * (e + b * seconds)) * decay;
    if (std::fabs(position_ - target_) < kRestDistance && std::fabs(velocity_) < kRestSpeed) {
        position_ = target_;
        velocity_ = 0.0;
        moving_ = settling_ = false;
    }
    moved();
}

double RingCarousel::position() const {
    if (count_ <= 0) return 0.0;
    const double n = static_cast<double>(count_);
    const double p = std::fmod(position_, n);
    return p < 0.0 ? p + n : p;
}

int RingCarousel::item() const {
    if (count_ <= 0) return 0;
    return static_cast<int>(((nearest_ % count_) + count_) % count_);
}

int RingCarousel::take_ticks() {
    const int ticks = ticks_;
    ticks_ = 0;
    return ticks;
}

void RingCarousel::moved() {
    const long long nearest = static_cast<long long>(std::floor(position_ + 0.5));
    ticks_ += static_cast<int>(nearest - nearest_);
    nearest_ = nearest;
}

}  // namespace sh::input
