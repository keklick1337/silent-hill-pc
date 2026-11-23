// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Template part of anim_player.h; include anim_player.h instead.

#pragma once

namespace sh::entities {

namespace detail {

inline constexpr std::int32_t kTimeOne = 4096;
inline constexpr std::int32_t kTimeHalf = 2048;

inline std::int32_t keyframe_time(std::int32_t keyframe) { return keyframe * kTimeOne; }
inline std::int32_t keyframe_of(std::int32_t time) { return time >> 12; }
inline std::int32_t fraction_of(std::int32_t time) { return time & 0xFFF; }
inline bool samples(const AnimState& state) { return (state.flags & (kAnimAdvances | kAnimPosed)) != 0; }

inline void link(AnimState& state, const ClipEntry& entry) {
    if (entry.next_status != kNoNextStatus) {
        state.status = entry.next_status;
    }
}

template <typename TimestepFn>
StepResult step_once(AnimState& state, const ClipEntry& entry, TimestepFn&& timestep) {
    StepResult r;
    const std::int32_t advance = timestep();
    std::int32_t time = state.time;
    if (advance != 0) {
        time += advance;
        const std::int32_t end = keyframe_time(entry.end_keyframe);
        const std::int32_t start = keyframe_time(entry.start_keyframe);
        if (time >= end) {
            time = end;
            r.linked = true;
        } else if (time <= start) {
            time = start;
            r.linked = true;
        }
    }
    const std::int32_t keyframe = keyframe_of(time);
    r.sample = samples(state);
    r.keyframe0 = keyframe;
    r.keyframe1 = keyframe + 1;
    r.alpha = fraction_of(time);
    state.time = time;
    state.keyframe = keyframe;
    state.alpha = 0;
    if (r.linked) {
        link(state, entry);
    }
    return r;
}

template <typename TimestepFn>
StepResult step_loop(AnimState& state, const ClipEntry& entry, TimestepFn&& timestep) {
    StepResult r;
    std::int32_t start = entry.start_keyframe;
    const std::int32_t end = entry.end_keyframe;
    // A loop without its own start continues from the current keyframe; with no valid current keyframe it
    // holds the end keyframe.
    if (start < 0) {
        start = state.keyframe;
        if (start < 0) {
            start = end;
        }
    }
    const std::int32_t next_start = end + 1;
    const std::int32_t start_time = keyframe_time(start);
    const std::int32_t next_start_time = keyframe_time(next_start);
    const std::int32_t length = keyframe_time(next_start - start);

    // A time set outside the loop by other code is pulled into it, not wrapped (a wrap would jump mid-loop).
    if (state.time >= next_start_time) {
        state.time = next_start_time - kTimeOne;
    } else if (state.time < start_time) {
        state.time = start_time;
    }

    std::int32_t time = state.time + timestep();
    if (length > 0) {
        while (time < start_time) {
            time += length;
        }
        while (time >= next_start_time) {
            time -= length;
        }
    }
    const std::int32_t keyframe0 = keyframe_of(time);
    std::int32_t keyframe1 = keyframe0 + 1;
    if (keyframe1 == next_start) {
        keyframe1 = start;
    }
    r.sample = samples(state);
    r.keyframe0 = keyframe0;
    r.keyframe1 = keyframe1;
    r.alpha = fraction_of(time);
    state.time = time;
    state.keyframe = keyframe0;
    state.alpha = 0;
    return r;
}

template <typename TimestepFn>
StepResult step_blend(AnimState& state, const ClipEntry& entry, TimestepFn&& timestep) {
    StepResult r;
    std::int32_t start = entry.start_keyframe;
    const std::int32_t end = entry.end_keyframe;
    if (start == kCurrentKeyframe) {
        start = state.keyframe;
    }
    if (start < 0) {
        start = end;
    }
    std::int32_t alpha = state.alpha + timestep();
    state.time = keyframe_time(alpha >= kTimeHalf ? end : start);
    if (alpha >= kTimeOne) {
        start = end;
        state.keyframe = end;
        alpha = 0;
        r.linked = true;
    }
    r.sample = samples(state);
    r.keyframe0 = start;
    r.keyframe1 = end;
    r.alpha = alpha;
    state.alpha = alpha;
    if (r.linked) {
        link(state, entry);
    }
    return r;
}

}  // namespace detail

template <typename TimestepFn>
StepResult step_animation(AnimState& state, const ClipEntry& entry, TimestepFn&& timestep) {
    switch (entry.kind) {
        case ClipKind::Blend:
            return detail::step_blend(state, entry, timestep);
        case ClipKind::Once:
            return detail::step_once(state, entry, timestep);
        case ClipKind::Loop:
            return detail::step_loop(state, entry, timestep);
        case ClipKind::None:
            break;
    }
    return {};
}

}  // namespace sh::entities
