// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/anim_player.h"

namespace sh::entities {

std::int32_t anim_timestep(std::int32_t speed, std::int32_t delta_time, std::uint8_t flags) {
    if ((flags & kAnimAdvances) == 0) {
        return 0;
    }
    return static_cast<std::int32_t>((std::int64_t{speed} * std::int64_t{delta_time}) >> 12);
}

StepResult step_animation_fixed(AnimState& state, const ClipEntry& entry, std::int32_t timestep) {
    return step_animation(state, entry, [timestep] { return timestep; });
}

bool keyframe_crossed(std::int32_t from, std::int32_t to, std::int32_t keyframe) {
    const std::int32_t a = from >> 12;
    const std::int32_t b = to >> 12;
    if (a == b) {
        return false;
    }
    return a < b ? (keyframe > a && keyframe <= b) : (keyframe < a && keyframe >= b);
}

}  // namespace sh::entities
