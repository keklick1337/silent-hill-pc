// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/phone_rumble.h"

#include <algorithm>
#include <cmath>

namespace sh::input {

namespace {

// The small motor of the pad only turns on or off; on its own it reads as a medium buzz.
constexpr float kSmallMotor = 0.5f;
// Under this a phone motor does not turn noticeably, so a weak rumble is lifted to it.
constexpr float kFloor = 0.12f;

}  // namespace

float phone_amplitude(const std::uint8_t* actuators, int count, float strength) {
    if (!actuators || count <= 0 || !(strength > 0.0f)) {
        return 0.0f;
    }
    const float small = actuators[0] != 0 ? kSmallMotor : 0.0f;
    const float large = count > 1 ? static_cast<float>(actuators[1]) / 255.0f : 0.0f;
    const float level = std::max(small, large);
    if (level <= 0.0f) {
        return 0.0f;
    }
    return std::clamp(level * std::min(strength, 1.0f), kFloor, 1.0f);
}

PhoneVibration PhoneRumble::frame(float amplitude, std::uint32_t now_ms) {
    PhoneVibration out;
    const int level =
        amplitude > 0.0f ? std::clamp(static_cast<int>(std::lround(amplitude * kSteps)), 1, kSteps) : 0;

    if (level == 0) {
        // The one-shot of a short buzz keeps running until it has been felt.
        if (!playing_ || now_ms - started_ms_ < kMinOnMs) {
            return out;
        }
        playing_ = false;
        level_ = 0;
        out.kind = PhoneVibration::Kind::Stop;
        return out;
    }

    if (playing_) {
        const std::uint32_t since = now_ms - sent_ms_;
        if (level == level_ ? since < kRefreshMs : since < kChangeMs) {
            return out;
        }
    } else {
        started_ms_ = now_ms;
    }
    playing_ = true;
    level_ = level;
    sent_ms_ = now_ms;
    out.kind = PhoneVibration::Kind::Play;
    out.amplitude = static_cast<float>(level) / static_cast<float>(kSteps);
    out.length_ms = kLengthMs;
    return out;
}

}  // namespace sh::input
