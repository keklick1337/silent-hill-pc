// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/world/aim_assist.h"

#include "engine/world/fixed_math.h"

namespace sh::world {

namespace {

// Port constants, bit-for-bit (Q12(x) truncates in float: 0.20 -> 819, 0.35 -> 1433).
constexpr std::int32_t kMouseRadiusMulQ12 = 10240; // mouse: hittable radius = 2.5x collision radius
constexpr std::int32_t kCtrlRadiusMulQ12 = 20480;  // controller: close-range floor radius = 5x
constexpr std::int32_t kCtrlConeTanQ12 = 819;      // controller: ~9deg magnetic cone, scaled by distance
constexpr std::int32_t kVertReachMulQ12 = 32768;   // vertical window reach = 8x radius
// Absolute floor on the mouse activation window: several enemies animate their
// collision radius down to ~0.05m, making 2.5x radius a few pixels wide — the
// reticle is visibly on the body but no snap fires. 0.35m of reticle slop
// cannot create false hits: the snap aims at the axis, so the bullet still has
// to thread the real (inflated) cylinder.
constexpr std::int32_t kMouseRadiusFloorQ12 = 1433;

}  // namespace

AimAssistResult aim_assist_pick(const AimAssistTarget* targets, std::size_t count,
                                std::int32_t cam_x, std::int32_t cam_y, std::int32_t cam_z,
                                std::int32_t fwd_x, std::int32_t fwd_y, std::int32_t fwd_z,
                                const AimAssistParams& params,
                                AimAssistOcclusion occluded, void* occlusion_ctx) {
    AimAssistResult best;
    std::int32_t best_k = 0x7FFFFFFF;
    std::int32_t best_perp = 0x7FFFFFFF;

    // |fwd_xz|^2 (Q24). Camera pitch is clamped well short of vertical, so this
    // is never near zero, but guard the actual divisor (fxz2 >> 12) so a
    // near-vertical forward can't trigger an integer div-by-zero below.
    const std::int64_t fxz2 =
        static_cast<std::int64_t>(fwd_x) * fwd_x + static_cast<std::int64_t>(fwd_z) * fwd_z;
    if ((fxz2 >> 12) <= 0) return best;

    for (std::size_t i = 0; i < count; i++) {
        const AimAssistTarget& t = targets[i];
        const std::int32_t dx = t.x - cam_x;
        const std::int32_t dz = t.z - cam_z;

        // kmult (Q12) is the multiplier on fwd putting the ray at its closest
        // horizontal approach to the enemy axis. Since |fwd| == Q12(1.0), kmult
        // is also the world distance along the ray to that point.
        const std::int32_t kmult = static_cast<std::int32_t>(
            (static_cast<std::int64_t>(dx) * fwd_x + static_cast<std::int64_t>(dz) * fwd_z) /
            (fxz2 >> 12));
        if (kmult <= 0 || kmult > params.range_q12) continue;  // behind the camera or past range

        const std::int32_t qx =
            cam_x + static_cast<std::int32_t>((static_cast<std::int64_t>(fwd_x) * kmult) >> 12);
        const std::int32_t qz =
            cam_z + static_cast<std::int32_t>((static_cast<std::int64_t>(fwd_z) * kmult) >> 12);
        const std::int32_t perp_h = planar_length(t.x - qx, t.z - qz);

        // ACTIVATION window: cover the full VISIBLE body (radius-based, since
        // the collision span underestimates it), so aiming at head/chest/legs
        // all trigger the assist. The aim point is then clamped back into the
        // real cylinder span below, so the damage ray still threads it.
        const std::int32_t body_h = t.y_high - t.y_low;
        std::int32_t v_slack =
            static_cast<std::int32_t>((static_cast<std::int64_t>(t.radius) * kVertReachMulQ12) >> 12);
        if (body_h > v_slack) v_slack = body_h;

        const std::int32_t ray_y =
            cam_y + static_cast<std::int32_t>((static_cast<std::int64_t>(fwd_y) * kmult) >> 12);
        if (ray_y < t.y_low - v_slack || ray_y > t.y_high + v_slack) continue;  // above/below the body

        std::int32_t max_perp;
        if (params.magnetic_cone) {
            const std::int32_t cone =
                static_cast<std::int32_t>((static_cast<std::int64_t>(kmult) * kCtrlConeTanQ12) >> 12);
            const std::int32_t floor_r =
                static_cast<std::int32_t>((static_cast<std::int64_t>(t.radius) * kCtrlRadiusMulQ12) >> 12);
            max_perp = (cone > floor_r) ? cone : floor_r;
        } else {
            max_perp =
                static_cast<std::int32_t>((static_cast<std::int64_t>(t.radius) * kMouseRadiusMulQ12) >> 12);
            if (max_perp < kMouseRadiusFloorQ12) max_perp = kMouseRadiusFloorQ12;
        }
        if (perp_h > max_perp) continue;  // reticle not over / near the body

        // Nearest qualifying enemy wins (tie-break: most on-target).
        if (kmult < best_k || (kmult == best_k && perp_h < best_perp)) {
            // Clamp the aim point into the EXPANDED bullet cylinder span (the
            // inflation the character trace applies during the player's bullet
            // trace) — not the narrow collision band — so a leg/torso shot aims
            // at that height and the impact (blood) lands where you aimed.
            const std::int32_t ev = params.expand_vertical
                ? static_cast<std::int32_t>((static_cast<std::int64_t>(t.radius) * params.expand_mul_q12) >> 12)
                : 0;
            const std::int32_t e_lo = t.y_low - ev;
            const std::int32_t e_hi = t.y_high + ev;
            AimAssistResult cand;
            cand.index = static_cast<std::int32_t>(i);
            cand.aim_x = t.x;
            cand.aim_y = (ray_y < e_lo) ? e_lo : (ray_y > e_hi) ? e_hi : ray_y;
            cand.aim_z = t.z;

            // Don't aim through walls: reject if level geometry blocks the eye
            // from the aim point. Skipped for an enemy on the camera's axis
            // (dx == dz == 0), where the query would test the shooter itself.
            if ((dx != 0 || dz != 0) && occluded != nullptr) {
                const std::int32_t dist_to_aim = planar_length(cand.aim_x - cam_x, cand.aim_z - cam_z);
                if (occluded(occlusion_ctx, cand.aim_x, cand.aim_y, cand.aim_z, dist_to_aim - t.radius))
                    continue;  // occluded
            }

            best = cand;
            best_k = kmult;
            best_perp = perp_h;
        }
    }
    return best;
}

}  // namespace sh::world
