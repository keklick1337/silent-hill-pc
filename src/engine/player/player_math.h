// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The integer arithmetic of Harry's movement and pose (Q12, the game's wrap-around 32-bit products). Pure
// functions: the Player class feeds them its state; tests and shadow checks call them directly.

#pragma once

#include <cstdint>

namespace sh::game::player_math {

// (a * b) >> 12 in 32 bits, and with a 64-bit product.
constexpr std::int32_t mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b)) >> 12;
}
constexpr std::int32_t mul_wide(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}
// x * dt measured against a 30 fps (or 60 fps) tick, as the game scales per-frame amounts.
constexpr std::int32_t per_tick_30(std::int32_t dt, std::int32_t x) { return (x * dt) / 136; }
constexpr std::int32_t per_tick_60(std::int32_t dt, std::int32_t x) { return (x * dt) / 68; }
constexpr std::int32_t wrap_angle(std::int32_t a) { return a & 0xFFF; }

// The speed along the heading after the slope under the feet takes its share: each axis is scaled by the square
// of cos(tilt / 8) of its slope, the result keeps the sign of the speed.
std::int32_t slope_speed(std::int32_t speed, std::int32_t heading, std::int32_t tilt_x, std::int32_t tilt_z);

// The world offset of a step of `step` (Q12 metres) along `heading`, without overflowing the 32-bit product:
// steps longer than half a 16-bit range are shifted down before the multiply and back after.
struct Offset {
    std::int32_t x = 0, z = 0;
};
Offset step_offset(std::int32_t step, std::int32_t heading);

// The ground the integration keeps after a move: `found` is the height the collision reported (32768: no floor),
// `previous` the last tick's ground, `y` the body's height before the move. Climbs and drops are limited per
// tick (the cell borders of the collision would teleport otherwise) and a floor two metres above the feet is
// refused as a phantom.
struct GroundClamp {
    std::int32_t height = 0;
    bool phantom = false;
};
GroundClamp clamp_ground(std::int32_t found, std::int32_t previous, std::int32_t y, std::int32_t dt);

// The results screen's totals: unsigned add, capped to [1, 2^31].
std::uint32_t add_distance(std::uint32_t total, std::int32_t step);

// How far the movement stick (-128..127 per axis) is pushed: |y| as the original, or with `radius`
// (GAMEPLAY CHANGE stick_radius) the radial deflection capped at 127.
std::int32_t stick_deflection(std::int32_t x, std::int32_t y, bool radius);

// Duration (keyframes per second, Q12) of a variable-speed clip of the legs by the stick: the walk, the run (whose
// stride frames slow down by the stick), the walk back and the tired idle.
enum class VariableClip : std::uint8_t { WalkForward, RunForward, WalkBackward, IdleExhausted, Other };
struct StickState {
    std::int32_t x = 0, y = 0;  // stick axes this tick (-128..127, forward negative)
    std::int32_t held = 0;      // deflection kept while the stick returns (0: none)
    bool radius = false;        // GAMEPLAY CHANGE stick_radius: push = radius of both axes
};
std::int32_t legs_clip_duration(VariableClip clip, const StickState& stick, std::int32_t keyframe,
                                std::int32_t health, std::int32_t exhaustion);

// The upper body's lean toward the aim in the classic camera (combat): the pitch and yaw the torso and arms take
// this tick from the aim pitch, the angle to the target and the aim hold.
struct AimLeanInput {
    std::int32_t aim_pitch = 0;        // camera-ray pitch of the aim (1024 = level)
    std::int32_t angle_to_target = 0;  // body yaw toward the target
    std::int32_t rotation_y = 0;
    std::int32_t aim_hold = 0;         // ticks the aim has settled (0: not yet)
    std::int32_t flex_target_x = 0;    // the pitch the aim eases to (0: none)
    std::int32_t weapon_attack = 0;
    bool lock_starting = false;        // the upper body is starting or switching a target lock
    bool settled = false;              // the aim-lock settled flag
    std::int32_t dt = 0;
};
struct Lean {
    std::int16_t x = 0, y = 0;
};
Lean aim_lean(const AimLeanInput& in, Lean current);
// The lean easing back to straight while Harry moves freely.
Lean relax_lean(Lean current, std::int32_t dt);

}  // namespace sh::game::player_math
