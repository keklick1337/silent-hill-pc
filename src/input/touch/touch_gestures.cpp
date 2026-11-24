// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/touch_gestures.h"

#include <algorithm>
#include <cmath>

namespace sh::input::touch {
namespace {

// The game reads a push as "v < -64" on the negative side and "v >= 64" on the positive one; 65 is past both.
constexpr int kFirstPush = 65;
constexpr int kFullPush = 127;

std::uint8_t to_byte(int offset) { return static_cast<std::uint8_t>(std::clamp(128 + offset, 0, 255)); }

}  // namespace

std::uint8_t axis_byte(float value, float dead) {
    const float a = std::fabs(value);
    if (a < dead || dead >= 1.0f) return 128;
    const float t = std::min((a - dead) / (1.0f - dead), 1.0f);
    const int push = kFirstPush + static_cast<int>(std::lround(t * static_cast<float>(kFullPush - kFirstPush)));
    return to_byte(value < 0.0f ? -push : push);
}

StickOutput stick_output(float dx, float dy, float radius, const StickTuning& tuning) {
    StickOutput out;
    if (radius <= 0.0f) return out;
    float x = dx / radius;
    float y = dy / radius;
    const float length = std::hypot(x, y);
    if (length > 1.0f) {
        x /= length;
        y /= length;
    }
    out.x = x;
    out.y = y;
    out.magnitude = std::min(length, 1.0f);
    out.axis_x = axis_byte(x, tuning.dead);
    out.axis_y = axis_byte(y, tuning.dead);
    // Any push the game reads as forward runs, a diagonal one too: Harry turns while he runs, as with the run button
    // held on a pad. A push that is no longer forward is a turn on the spot or a step back: no running backwards.
    const bool forward = y < -tuning.dead;
    out.run = tuning.run && forward && out.magnitude >= (tuning.running ? tuning.run_keep : tuning.run_at);
    return out;
}

Point follow_origin(Point origin, Point finger, float radius, float follow) {
    const float dx = finger.x - origin.x;
    const float dy = finger.y - origin.y;
    const float d = std::hypot(dx, dy);
    const float reach = radius * follow;
    if (d <= reach || d <= 0.0f) return origin;
    const float k = (d - reach) / d;
    return {origin.x + dx * k, origin.y + dy * k};
}

namespace {

// Sector 0 is right, then clockwise on the screen (+y down) in steps of 45 degrees.
constexpr std::array<std::uint8_t, 8> kSectorBits = {
    kDpadRight, kDpadRight | kDpadDown, kDpadDown, kDpadDown | kDpadLeft,
    kDpadLeft,  kDpadLeft | kDpadUp,    kDpadUp,   kDpadUp | kDpadRight,
};

constexpr float kPi = 3.14159265f;
constexpr float kSector = kPi / 4.0f;

int sector_of_bits(std::uint8_t bits) {
    for (std::size_t i = 0; i < kSectorBits.size(); ++i)
        if (kSectorBits[i] == bits) return static_cast<int>(i);
    return -1;
}

// The smallest angle between two directions, 0..pi.
float angle_between(float a, float b) {
    float d = std::fmod(std::fabs(a - b), 2.0f * kPi);
    return d > kPi ? 2.0f * kPi - d : d;
}

}  // namespace

std::uint8_t dpad_bits(float dx, float dy, float radius, std::uint8_t previous, const DpadTuning& tuning) {
    if (radius <= 0.0f) return 0;
    const float distance = std::hypot(dx, dy) / radius;
    const int held = sector_of_bits(previous);
    if (distance < (held >= 0 ? tuning.dead_keep : tuning.dead)) return 0;
    const float angle = std::atan2(dy, dx);
    if (held >= 0 && angle_between(angle, static_cast<float>(held) * kSector) <= 0.5f * kSector + tuning.hysteresis)
        return previous;
    const int sector = static_cast<int>(std::lround(angle / kSector));
    return kSectorBits[static_cast<std::size_t>(((sector % 8) + 8) % 8)];
}

void Visibility::update(const Facts& facts, float seconds) {
    seconds = std::max(seconds, 0.0f);
    gameplay_for_ = facts.gameplay ? gameplay_for_ + seconds : 0.0f;
    shown_ = facts.gameplay && gameplay_for_ >= kShowDelay;
    since_target_ = facts.action_target ? 0.0f : since_target_ + seconds;

    std::uint32_t want = 0;
    if (shown_) {
        want = bit(Control::Map) | bit(Control::Inventory) | bit(Control::Pause) | bit(Control::Camera);
        want |= facts.dpad ? bit(Control::Dpad) | bit(Control::Run) : bit(Control::Stick);
        if ((facts.prompted & bit(Control::Struggle)) != 0) {
            // The struggle button takes the right thumb's place: nothing else there answers while Harry is held.
            want |= bit(Control::Struggle);
        } else {
            if (facts.armed) want |= bit(Control::Aim);
            if (facts.action_always || since_target_ <= kActionLinger) want |= bit(Control::Action);
            if (facts.aiming || (facts.prompted & bit(Control::Attack)) != 0) want |= bit(Control::Attack);
        }
    }
    visible_ = want;
    const float step = kFade > 0.0f ? seconds / kFade : 1.0f;
    for (int i = 0; i < kControlCount; ++i) {
        float& a = alpha_[static_cast<std::size_t>(i)];
        const bool on = (want & (1u << i)) != 0;
        // Leaving gameplay takes the controls away at once: a screen opened over the game is not to be covered.
        a = !shown_ ? 0.0f : on ? std::min(a + step, 1.0f) : std::max(a - step, 0.0f);
    }
}

void Visibility::hide() {
    gameplay_for_ = 0.0f;
    shown_ = false;
    visible_ = 0;
    alpha_.fill(0.0f);
}

void PressTrain::add() { queued_ = std::min(queued_ + 1, kMaxQueued); }

bool PressTrain::step() {
    if (frame_ == 0) {
        if (queued_ == 0) return false;
        --queued_;
        frame_ = 1;
        return true;
    }
    ++frame_;
    if (frame_ > kHold + kGap) {
        frame_ = 0;
        return step();
    }
    return frame_ <= kHold;
}

bool Flicks::update(float x, float y) {
    const float m = std::hypot(x, y);
    if (m < kRest) {
        armed_ = true;
        return false;
    }
    if (m < kPush) return false;
    const float dir = std::atan2(y, x);
    bool flick = armed_;
    if (!flick && have_dir_) {
        float turn = std::fabs(dir - dir_);
        if (turn > 3.14159265f) turn = 2.0f * 3.14159265f - turn;
        flick = turn > kTurn;
    }
    if (flick) {
        armed_ = false;
        have_dir_ = true;
        dir_ = dir;
    }
    return flick;
}

bool is_tap(float seconds, float moved, float slop) { return seconds <= kTapSeconds && moved <= slop; }

bool pointer_tap(float moved, float slop, bool stop_only) { return !stop_only && moved <= slop; }

void Pinch::start(float distance) {
    start_ = distance > 1.0f ? distance : 0.0f;
    fired_ = false;
}

int Pinch::update(float distance) {
    if (start_ <= 0.0f || fired_) return 0;
    const float ratio = distance / start_;
    if (ratio >= kRatio) {
        fired_ = true;
        return 1;
    }
    if (ratio <= 1.0f / kRatio) {
        fired_ = true;
        return -1;
    }
    return 0;
}

void Scroller::press() {
    velocity_ = 0.0f;
    sample_ = 0.0f;
    carry_ = 0.0f;
    held_ = true;
}

int Scroller::take(float distance, float step) {
    if (step <= 0.0f) return 0;
    carry_ += distance;
    const int notches = static_cast<int>(carry_ / step);
    carry_ -= static_cast<float>(notches) * step;
    return notches;
}

void Scroller::sample(float delta, float seconds) {
    if (seconds <= 0.0f) return;
    const float speed = std::clamp(delta / seconds, -kMaxVelocity, kMaxVelocity);
    // Smoothed over a few frames: the last frame's move alone is too jumpy to throw a list with.
    sample_ = sample_ * 0.5f + speed * 0.5f;
}

int Scroller::drag(float delta, float seconds, float step) {
    sample(delta, seconds);
    return take(delta, step);
}

void Scroller::release() {
    held_ = false;
    velocity_ = std::fabs(sample_) >= kMinVelocity * 4.0f ? sample_ : 0.0f;
}

int Scroller::glide(float seconds, float step) {
    if (held_ || velocity_ == 0.0f || seconds <= 0.0f) return 0;
    const int notches = take(velocity_ * seconds, step);
    velocity_ *= std::exp(-kFriction * seconds);
    if (std::fabs(velocity_) < kMinVelocity) stop();
    return notches;
}

std::uint8_t pan_axis(float speed_per_height, float gain) {
    const float push = speed_per_height * gain;
    if (std::fabs(push) < 1.0f) return 128;
    const int offset = std::min(kFirstPush + static_cast<int>(std::fabs(push)), kFullPush);
    return to_byte(push < 0.0f ? -offset : offset);
}

}  // namespace sh::input::touch
