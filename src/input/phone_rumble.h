// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The pad rumble played on a phone's own vibrator (sdl_pad.cpp, Android, when no pad with motors is in use): the two
// motors of the actuator bytes as one amplitude, and when to send the vibrator a new one-shot. A phone vibrator call
// restarts the motor and crosses into Java, so a new one goes out only when the level changes (at most every
// kChangeMs) or the running one-shot is about to end; and a buzz of a single frame is held kMinOnMs, which is about
// what a phone motor needs to be felt at all. No SDL.

#pragma once

#include <cstdint>

namespace sh::input {

struct PhoneVibration {
    enum class Kind : std::uint8_t { None, Play, Stop };
    Kind kind = Kind::None;
    float amplitude = 0.0f;  // 0..1, Play only
    std::uint32_t length_ms = 0;
};

// The level of a pad frame's actuator bytes (byte 0 the small on/off motor, byte 1 the large one, 0..255) on one
// vibrator, scaled by `strength` (config.cfg phone_vibration, 0..1). 0 when no motor turns or strength is 0.
float phone_amplitude(const std::uint8_t* actuators, int count, float strength);

class PhoneRumble {
public:
    static constexpr std::uint32_t kLengthMs = 300;   // one one-shot; a stalled game stops vibrating after this
    static constexpr std::uint32_t kRefreshMs = 200;  // a steady level is renewed this often
    static constexpr std::uint32_t kChangeMs = 50;    // a changed level waits this long after the last call
    static constexpr std::uint32_t kMinOnMs = 50;     // the shortest buzz
    static constexpr int kSteps = 32;                 // amplitude steps; a smaller wander is not resent

    // One frame at `now_ms`: what to send the vibrator.
    PhoneVibration frame(float amplitude, std::uint32_t now_ms);
    bool playing() const { return playing_; }

private:
    bool playing_ = false;
    int level_ = 0;  // in kSteps
    std::uint32_t sent_ms_ = 0;
    std::uint32_t started_ms_ = 0;  // when the vibrator last started from rest
};

}  // namespace sh::input
