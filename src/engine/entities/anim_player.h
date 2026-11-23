// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Animation playback of the entity module: the per-tick state machine of a character's animation, written from
// the behaviour of the game (docs/FORMATS.md).
//
// A character plays one entry of its clip table at a time. The entry index is the packed `status`:
// status = clip * 2 + phase, phase 0 the blend into the clip, phase 1 the clip itself. An entry is a blend, a
// one-shot or a loop over keyframes [start, end] at `speed` keyframes per second (Q12; negative plays a one-shot
// backwards). Times are Q12 keyframes: time >> 12 is the keyframe, time & 0xFFF the fraction towards the next one.
// When a blend or a one-shot reaches its end the status moves to the entry's `next_status` (kNoNextStatus: stays).
//
// step() advances one tick and says which two keyframes to sample at which alpha; it does not touch bones.

#pragma once

#include <cstdint>

namespace sh::entities {

enum class ClipKind : std::uint8_t {
    Blend = 0,  // blend from the current keyframe (start = kCurrentKeyframe) to `end`, alpha grows by speed * dt
    Once = 1,   // start..end once, then next_status
    Loop = 2,   // start..end, wrapping
    None = 3,   // no playback
};

inline constexpr std::uint8_t kNoNextStatus = 0xFF;
inline constexpr std::int16_t kCurrentKeyframe = -1;

// Bits of AnimState::flags.
inline constexpr std::uint8_t kAnimAdvances = 1 << 0;  // time moves (otherwise the pose holds)
inline constexpr std::uint8_t kAnimPosed = 1 << 1;     // bones are sampled even while time does not move

struct ClipEntry {
    ClipKind kind = ClipKind::None;
    std::uint8_t next_status = kNoNextStatus;
    std::int16_t start_keyframe = kCurrentKeyframe;
    std::int16_t end_keyframe = 0;
    std::int32_t speed = 0;  // Q12 keyframes per second (or alpha per second for a blend)
};

struct AnimState {
    std::uint8_t status = 0;
    std::uint8_t flags = 0;
    std::int32_t time = 0;      // Q12 keyframes
    std::int32_t keyframe = 0;  // time >> 12 after the last step (blend: its start keyframe until it ends)
    std::int32_t alpha = 0;     // blend progress, Q12
    bool operator==(const AnimState&) const = default;
};

struct StepResult {
    bool sample = false;  // flags has kAnimAdvances or kAnimPosed
    std::int32_t keyframe0 = 0;
    std::int32_t keyframe1 = 0;
    std::int32_t alpha = 0;     // Q12 weight of keyframe1
    bool linked = false;        // the entry ended this tick (status moved unless next_status is kNoNextStatus)
    bool operator==(const StepResult&) const = default;
};

// Time the entry moves this tick: speed * dt (both Q12, 64-bit product >> 12) when the animation advances, else 0.
std::int32_t anim_timestep(std::int32_t speed, std::int32_t delta_time, std::uint8_t flags);

// One tick of `entry` on `state`. `timestep` is the Q12 advance (anim_timestep); it is a callable because a
// character may compute its speed from its state (Harry's run speed), and the game reads that speed after a loop
// has pulled an out-of-range time back into range.
template <typename TimestepFn>
StepResult step_animation(AnimState& state, const ClipEntry& entry, TimestepFn&& timestep);

// The same with a fixed timestep.
StepResult step_animation_fixed(AnimState& state, const ClipEntry& entry, std::int32_t timestep);

// True when time moving from `from` to `to` (Q12) passes keyframe `keyframe` (enters it this tick): the frame
// events of a clip (footsteps, attack frames).
bool keyframe_crossed(std::int32_t from, std::int32_t to, std::int32_t keyframe);

}  // namespace sh::entities

#include "engine/entities/anim_player_impl.h"
