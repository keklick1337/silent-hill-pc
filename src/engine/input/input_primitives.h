// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Pure helpers for a data-driven player controller: input history registers aged at the
// original 30 Hz game-tick cadence (the original aged them once per tick while OR-ing the
// current input every frame, so aging and pushing are separate operations), a Q12 tick gate
// with the original's remainder-discard quirk, and an asymmetric analog dead zone (the
// negative side needs one unit more deflection than the positive side, as in the original).
// No game types, no PS1 bits: everything is plain integers.

#pragma once

#include <cstdint>

namespace sh::input {

// ---- input history registers ----------------------------------------------------------

constexpr std::uint32_t shift_mask(unsigned width) { return (1u << width) - 1u; }

// Drops the oldest bit and clears bit 0 for the new input.
constexpr std::uint32_t shift_age(std::uint32_t value, unsigned width) {
    return (value * 2u) & shift_mask(width);
}

// ORs the current input into bit 0 without aging.
constexpr std::uint32_t shift_push(std::uint32_t value, bool current) {
    return value | (current ? 1u : 0u);
}

class ShiftRegister {
public:
    explicit constexpr ShiftRegister(unsigned width, std::uint32_t value = 0)
        : width_(width), value_(value & shift_mask(width)) {}

    void age() { value_ = shift_age(value_, width_); }
    void push(bool current) { value_ = shift_push(value_, current); }

    std::uint32_t value() const { return value_; }
    void set(std::uint32_t v) { value_ = v & shift_mask(width_); }
    unsigned width() const { return width_; }

private:
    unsigned width_;
    std::uint32_t value_;
};

// ---- 30 Hz tick gate -------------------------------------------------------------------

// Q12(1/30) truncated: 0.0333f * 4096 = 136.53 -> 136.
inline constexpr std::int32_t kTick30Q12 = 136;

// At most one tick per call. A remainder strictly greater than the tick length is discarded
// to 0 (never two ticks out of one frame); a remainder of exactly the tick length is KEPT and
// fires on the next call. No clamping when not ready; a negative delta is simply added.
bool tick30_ready(std::int32_t& accumulator_q12, std::int32_t delta_q12);

class Tick30Gate {
public:
    bool ready(std::int32_t delta_q12) { return tick30_ready(accumulator_, delta_q12); }
    std::int32_t accumulator() const { return accumulator_; }
    void reset() { accumulator_ = 0; }

private:
    std::int32_t accumulator_ = 0;
};

// ---- analog dead zone ------------------------------------------------------------------

// The dead zone is asymmetric: the negative side is "v < -t", the positive "v >= t".
inline constexpr int kStickThreshold = 64;

constexpr bool axis_negative(int v, int t = kStickThreshold) { return v < -t; }
constexpr bool axis_positive(int v, int t = kStickThreshold) { return v >= t; }

constexpr bool stick_outside_deadzone(int x, int y, int t = kStickThreshold) {
    return axis_negative(x, t) || axis_positive(x, t) || axis_negative(y, t) || axis_positive(y, t);
}

// How far past the zone an axis is (0 inside it): -128 -> 64, -65 -> 1, -64 -> 0.
// v + t is always negative here (v < -t), so the negation is the magnitude.
constexpr int axis_negative_excess(int v, int t = kStickThreshold) {
    return v < -t ? -(v + t) : 0;
}

// 127 -> 64, 64 -> 1, 63 -> 0.
constexpr int axis_positive_excess(int v, int t = kStickThreshold) {
    return v >= t ? v - (t - 1) : 0;
}

}  // namespace sh::input
