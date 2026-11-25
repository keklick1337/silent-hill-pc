// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The pad rumble on a phone's vibrator (src/input/phone_rumble.h): the motors as one amplitude and when a one-shot
// is sent.

#include "framework/sh_test.h"

#include "input/phone_rumble.h"

#include <cmath>
#include <cstdint>

using namespace sh::input;

namespace {

bool close_to(float a, float b) { return std::fabs(a - b) < 1e-4f; }

using Kind = PhoneVibration::Kind;

}  // namespace

SH_TEST(phone_amplitude_of_the_motors) {
    const std::uint8_t off[2] = {0, 0};
    const std::uint8_t small[2] = {1, 0};
    const std::uint8_t large[2] = {0, 255};
    const std::uint8_t both[2] = {1, 51};
    const std::uint8_t weak[2] = {0, 10};
    SH_CHECK(phone_amplitude(off, 2, 1.0f) == 0.0f);
    SH_CHECK(phone_amplitude(nullptr, 2, 1.0f) == 0.0f);
    SH_CHECK(phone_amplitude(large, 0, 1.0f) == 0.0f);
    SH_CHECK(close_to(phone_amplitude(small, 2, 1.0f), 0.5f));
    SH_CHECK(close_to(phone_amplitude(small, 1, 1.0f), 0.5f));  // a buffer of the small motor only
    SH_CHECK(close_to(phone_amplitude(large, 2, 1.0f), 1.0f));
    // The stronger motor wins.
    SH_CHECK(close_to(phone_amplitude(both, 2, 1.0f), 0.5f));
    // The strength scales; 0 is off, a weak level is lifted to what a phone motor can be felt at.
    SH_CHECK(close_to(phone_amplitude(large, 2, 0.5f), 0.5f));
    SH_CHECK(phone_amplitude(large, 2, 0.0f) == 0.0f);
    SH_CHECK(close_to(phone_amplitude(weak, 2, 1.0f), 0.12f));
    SH_CHECK(close_to(phone_amplitude(large, 2, 2.0f), 1.0f));
}

SH_TEST(phone_rumble_steady_level_is_renewed) {
    PhoneRumble r;
    PhoneVibration v = r.frame(1.0f, 1000);
    SH_CHECK(v.kind == Kind::Play && close_to(v.amplitude, 1.0f) && v.length_ms == PhoneRumble::kLengthMs);
    SH_CHECK(r.playing());
    // The same level is not resent every frame, only before the one-shot ends.
    SH_CHECK(r.frame(1.0f, 1016).kind == Kind::None);
    SH_CHECK(r.frame(1.0f, 1000 + PhoneRumble::kRefreshMs - 1).kind == Kind::None);
    SH_CHECK(r.frame(1.0f, 1000 + PhoneRumble::kRefreshMs).kind == Kind::Play);
    SH_CHECK(PhoneRumble::kRefreshMs < PhoneRumble::kLengthMs);
}

SH_TEST(phone_rumble_changes_are_paced) {
    PhoneRumble r;
    SH_CHECK(r.frame(0.5f, 0).kind == Kind::Play);
    // A wander smaller than a step is the same level.
    SH_CHECK(r.frame(0.505f, 16).kind == Kind::None);
    // A new level waits until kChangeMs after the last call, then goes out at once.
    SH_CHECK(r.frame(0.9f, 16).kind == Kind::None);
    PhoneVibration v = r.frame(0.9f, PhoneRumble::kChangeMs);
    SH_CHECK(v.kind == Kind::Play && close_to(v.amplitude, 29.0f / 32.0f));
}

SH_TEST(phone_rumble_short_buzz_is_held) {
    PhoneRumble r;
    SH_CHECK(r.frame(0.0f, 0).kind == Kind::None);  // at rest nothing is sent
    SH_CHECK(r.frame(0.8f, 100).kind == Kind::Play);
    // One frame of rumble: the stop waits until the buzz has lasted kMinOnMs.
    SH_CHECK(r.frame(0.0f, 116).kind == Kind::None);
    SH_CHECK(r.playing());
    SH_CHECK(r.frame(0.0f, 100 + PhoneRumble::kMinOnMs).kind == Kind::Stop);
    SH_CHECK(!r.playing());
    SH_CHECK(r.frame(0.0f, 300).kind == Kind::None);
    // A longer rumble stops on the first silent frame.
    SH_CHECK(r.frame(0.8f, 400).kind == Kind::Play);
    SH_CHECK(r.frame(0.0f, 400 + PhoneRumble::kMinOnMs + 10).kind == Kind::Stop);
}

SH_TEST(phone_rumble_survives_the_tick_wrap) {
    PhoneRumble r;
    const std::uint32_t t = 0xFFFFFFF0u;
    SH_CHECK(r.frame(1.0f, t).kind == Kind::Play);
    SH_CHECK(r.frame(1.0f, t + 16).kind == Kind::None);  // wrapped to 0
    SH_CHECK(r.frame(1.0f, t + PhoneRumble::kRefreshMs).kind == Kind::Play);
}
