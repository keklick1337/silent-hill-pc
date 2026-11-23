// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Free-aim aim assist: which enemy, if any, a screen-center bullet ray should
// snap onto.
//
// Free-aim casts a single screen-center ray; an enemy only takes a hit when
// that ray threads its tight collision cylinder AND the separate hand->target
// damage ray (offset from the camera eye by parallax) threads it too. The
// picker returns a point ON the enemy's vertical axis at the aimed height, so
// pointing anywhere at the body guarantees the damage ray threads the cylinder.
//
// The body is modelled as a vertical capsule [y_low..y_high] of `radius` — the
// same extents the character trace uses for bullets. Nearest qualifying enemy
// wins; ties go to the one most on-target. Plain Q12 integers, no game types;
// the caller filters candidates (alive, damageable, positive radius) and
// provides the level-geometry occlusion test.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sh::world {

// One already-filtered candidate. All values are world Q12.
struct AimAssistTarget {
    std::int32_t x;      // horizontal centre of the enemy's axis
    std::int32_t z;
    std::int32_t y_low;  // vertical body span (min/max of the collision box)
    std::int32_t y_high;
    std::int32_t radius; // scaled collision-cylinder radius, > 0
};

// Occlusion test: can the eye NOT see (x,y,z) because level geometry blocks it,
// given `free_dist` of clear horizontal space the ray may traverse (the
// distance to the aim point minus the target radius)? Returns 1 when occluded,
// 0 when visible. Plain int so a C ABI region can implement it too. Nullptr
// treats every point as visible.
using AimAssistOcclusion = int (*)(void* ctx, std::int32_t x, std::int32_t y, std::int32_t z,
                                   std::int32_t free_dist);

struct AimAssistParams {
    bool magnetic_cone;          // controller: magnetic auto-aim window instead of the tight mouse window
    std::int32_t range_q12;      // aim range along the ray
    bool expand_vertical;        // fat bullet hitbox: clamp the aim point into the expanded span
    std::int32_t expand_mul_q12; // vertical reach added each side = this * radius (Q12 multiplier, 8192 = 2.0x)
};

struct AimAssistResult {
    std::int32_t index = -1;  // index into targets, -1 when nothing qualifies
    std::int32_t aim_x = 0;
    std::int32_t aim_y = 0;
    std::int32_t aim_z = 0;
};

// cam_* is the eye; fwd_* is the unit Q12 view forward (|fwd| == 4096).
AimAssistResult aim_assist_pick(const AimAssistTarget* targets, std::size_t count,
                                std::int32_t cam_x, std::int32_t cam_y, std::int32_t cam_z,
                                std::int32_t fwd_x, std::int32_t fwd_y, std::int32_t fwd_z,
                                const AimAssistParams& params,
                                AimAssistOcclusion occluded, void* occlusion_ctx);

}  // namespace sh::world
