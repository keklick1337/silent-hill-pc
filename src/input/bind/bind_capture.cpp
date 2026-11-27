// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/bind/bind_capture.h"

#include "input/bind/pad_chord.h"

#include <algorithm>

namespace sh::input {
namespace {

// Keyboard positions (USB HID usage numbers).
constexpr std::size_t kEscape = 41;
constexpr std::size_t kCapsLock = 57;
constexpr std::size_t kScrollLock = 71;
constexpr std::size_t kNumLock = 83;

constexpr unsigned kAllMouseButtons = 0x1F;

bool held(std::span<const std::uint8_t> keys, std::size_t position) {
    return position < keys.size() && keys[position] != 0;
}

// A lock that is switched on reads as a held key on some systems.
bool is_lock(std::size_t position) {
    return position == kCapsLock || position == kScrollLock || position == kNumLock;
}

BindCapture::Result waiting() { return {}; }
BindCapture::Result cancelled() { return {BindCapture::Status::Cancelled, {}}; }
BindCapture::Result captured(std::string name) { return {BindCapture::Status::Captured, std::move(name)}; }

}  // namespace

void MenuNav::reset() {
    was_held_.fill(true);
    repeat_at_.fill(0);
}

NavState MenuNav::step(const NavState& held_now, std::uint32_t now_ms) {
    NavState pressed;
    for (std::size_t i = 0; i < static_cast<std::size_t>(kNavKeys); ++i) {
        const bool repeats = i <= static_cast<std::size_t>(NavKey::Right);
        if (held_now.on[i] && !was_held_[i]) {
            pressed.on[i] = true;
            repeat_at_[i] = now_ms + kRepeatDelayMs;
        } else if (held_now.on[i] && repeats && now_ms >= repeat_at_[i]) {
            pressed.on[i] = true;
            repeat_at_[i] = now_ms + kRepeatEveryMs;
        }
        was_held_[i] = held_now.on[i];
    }
    return pressed;
}

void BindCapture::begin(const CaptureTarget& target, CaptureFilter filter, std::uint32_t now_ms) {
    target_ = target;
    filter_ = filter;
    started_ms_ = now_ms;
    idle_since_ms_ = now_ms;
    armed_ = false;
    pad_order_.clear();
    pad_chosen_.clear();
}

BindCapture::Result BindCapture::step(const CaptureInput& in) {
    return target_.pad ? step_pad(in) : step_keys(in);
}

BindCapture::Result BindCapture::step_pad(const CaptureInput& in) {
    if (held(in.keys, kEscape)) {
        return cancelled();
    }
    if (!in.pad_connected) {
        return cancelled();
    }
    if (!armed_) {
        if (in.now_ms - started_ms_ > kPadTimeoutMs) {
            return cancelled();
        }
        armed_ = in.pad_buttons.empty();
        return waiting();
    }

    std::erase_if(pad_order_, [&](const std::string& name) {
        return std::find(in.pad_buttons.begin(), in.pad_buttons.end(), name) == in.pad_buttons.end();
    });
    for (const std::string& name : in.pad_buttons) {
        if (std::find(pad_order_.begin(), pad_order_.end(), name) == pad_order_.end()) {
            pad_order_.push_back(name);
        }
    }

    if (pad_order_.empty()) {
        if (!pad_chosen_.empty()) {
            return captured(chord_value(pad_chosen_));
        }
        return in.now_ms - idle_since_ms_ > kPadTimeoutMs ? cancelled() : waiting();
    }
    idle_since_ms_ = in.now_ms;
    // The first inputs held together, as many as the cell takes; letting one go and pressing another keeps them.
    const std::size_t takes = target_.chord ? kChordMaxButtons : 1;
    if (pad_chosen_.size() < takes && pad_order_.size() > pad_chosen_.size()) {
        pad_chosen_.assign(pad_order_.begin(),
                           pad_order_.begin() + static_cast<std::ptrdiff_t>(std::min(takes, pad_order_.size())));
    }
    return waiting();
}

BindCapture::Result BindCapture::step_keys(const CaptureInput& in) {
    const std::size_t count = std::min(in.keys.size(), kPositions);

    if (!armed_) {
        bool busy = (in.mouse_buttons & kAllMouseButtons) != 0;
        for (std::size_t p = 1; p < in.keys.size() && !busy; ++p) {
            busy = in.keys[p] != 0 && !is_lock(p);
        }
        if (!busy) {
            armed_ = true;
            std::copy_n(in.keys.begin(), count, last_keys_.begin());
        }
        return waiting();
    }

    if (held(in.keys, kEscape)) {
        return cancelled();
    }

    // Only a key going down counts, so a lock that read as held from the start is never taken. A mouse-only
    // capture takes no key but keeps following them.
    for (std::size_t p = 1; p < count; ++p) {
        const bool went_down = in.keys[p] != 0 && last_keys_[p] == 0;
        last_keys_[p] = in.keys[p];
        if (!went_down || filter_ == CaptureFilter::MouseOnly) {
            continue;
        }
        std::string name = in.key_name ? in.key_name(static_cast<int>(p)) : std::string();
        if (!name.empty()) {
            return captured(std::move(name));
        }
    }

    if (target_.mouse && filter_ != CaptureFilter::KeysOnly) {
        for (int n = 1; n <= 5; ++n) {
            if ((in.mouse_buttons & (1u << (n - 1))) != 0) {
                return captured("Mouse" + std::to_string(n));
            }
        }
        if (target_.wheel) {
            if (in.wheel_up) {
                return captured("MouseWheelUp");
            }
            if (in.wheel_down) {
                return captured("MouseWheelDown");
            }
        }
    }
    return waiting();
}

}  // namespace sh::input
