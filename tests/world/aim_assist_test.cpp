// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/world/aim_assist.h"

#include <vector>

using namespace sh::world;

namespace {

struct OccCtx {
    bool block_all = false;
    int queries = 0;
};

int occ(void* ctxp, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {
    OccCtx* ctx = static_cast<OccCtx*>(ctxp);
    ctx->queries++;
    return ctx->block_all ? 1 : 0;
}

// Unit Q12 forward looking down +x, pitched flat.
constexpr std::int32_t kFwd[3] = {4096, 0, 0};

AimAssistParams mouse_params(std::int32_t range) {
    AimAssistParams p;
    p.magnetic_cone = false;
    p.range_q12 = range;
    p.expand_vertical = false;
    p.expand_mul_q12 = 8192;
    return p;
}

}  // namespace

SH_TEST(aim_assist_picks_enemy_on_axis) {
    AimAssistTarget t;
    t.x = 40960;  // 10 units ahead
    t.z = 0;
    t.y_low = 0;
    t.y_high = 8192;
    t.radius = 1024;

    OccCtx ctx;
    const AimAssistResult r =
        aim_assist_pick(&t, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], mouse_params(81920), occ, &ctx);
    SH_CHECK(r.index == 0);
    SH_CHECK(r.aim_x == 40960);
    SH_CHECK(r.aim_z == 0);
    SH_CHECK(r.aim_y == 4096);  // camera height, inside the body span
    SH_CHECK(ctx.queries == 1);
}

SH_TEST(aim_assist_rejects_behind_and_beyond_range) {
    AimAssistTarget behind;
    behind.x = -40960;
    behind.z = 0;
    behind.y_low = 0;
    behind.y_high = 8192;
    behind.radius = 1024;

    OccCtx ctx;
    SH_CHECK(aim_assist_pick(&behind, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], mouse_params(81920), occ, &ctx)
                 .index == -1);

    // Ahead but past the aim range.
    behind.x = 40960;
    SH_CHECK(aim_assist_pick(&behind, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], mouse_params(20480), occ, &ctx)
                 .index == -1);
}

SH_TEST(aim_assist_vertical_window_and_clamp) {
    AimAssistTarget t;
    t.x = 40960;
    t.z = 0;
    // Narrow neck band at head height; the activation window reaches 8x radius
    // around it, but the aim point clamps into the (possibly expanded) band.
    t.y_low = 7000;
    t.y_high = 7100;
    t.radius = 512;

    OccCtx ctx;
    // Aim well below the band: still activates (window), clamps to the band.
    const AimAssistResult r =
        aim_assist_pick(&t, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], mouse_params(81920), occ, &ctx);
    SH_CHECK(r.index == 0);
    SH_CHECK(r.aim_y == 7000);

    // With vertical expansion (fat hitbox) the clamp span grows by mul*radius.
    AimAssistParams p = mouse_params(81920);
    p.expand_vertical = true;
    p.expand_mul_q12 = 8192;  // 2.0x radius each side = 1024
    const AimAssistResult r2 = aim_assist_pick(&t, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], p, occ, &ctx);
    SH_CHECK(r2.index == 0);
    SH_CHECK(r2.aim_y == 7000 - 1024);

    // Far above the body: no activation at all.
    const AimAssistResult r3 =
        aim_assist_pick(&t, 1, 0, 40000, 0, kFwd[0], kFwd[1], kFwd[2], mouse_params(81920), occ, &ctx);
    SH_CHECK(r3.index == -1);
}

SH_TEST(aim_assist_mouse_floor_and_controller_cone) {
    // Tiny radius: the mouse window floors at Q12(0.35) instead of 2.5x almost
    // nothing, so a small offset still snaps.
    AimAssistTarget t;
    t.x = 40960;
    t.z = 1000;  // ~0.24 units off the ray
    t.y_low = 0;
    t.y_high = 4096;
    t.radius = 200;  // 2.5x radius = 500 < 1433: the floor decides

    OccCtx ctx;
    SH_CHECK(aim_assist_pick(&t, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], mouse_params(81920), occ, &ctx)
                 .index == 0);

    // Controller cone at 10 units: 10*4096*tan -> 8190-ish window, far wider.
    AimAssistParams pad = mouse_params(81920);
    pad.magnetic_cone = true;
    t.z = 6000;
    SH_CHECK(aim_assist_pick(&t, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], pad, occ, &ctx).index == 0);
}

SH_TEST(aim_assist_nearest_wins_and_tie_breaks_on_target) {
    std::vector<AimAssistTarget> ts(3);
    ts[0].x = 81920;
    ts[0].z = 0;
    ts[1].x = 40960;
    ts[1].z = 300;  // nearer but off-axis
    ts[2].x = 40960;
    ts[2].z = 100;  // nearer and more on-target than ts[1]
    for (auto& t : ts) {
        t.y_low = 0;
        t.y_high = 8192;
        t.radius = 4096;
    }

    OccCtx ctx;
    const AimAssistResult r = aim_assist_pick(ts.data(), ts.size(), 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2],
                                              mouse_params(81920), occ, &ctx);
    SH_CHECK(r.index == 2);  // equal distance with ts[1], more on-target wins

    // If the closer pair is removed, the far one still qualifies.
    ts.erase(ts.begin() + 1, ts.begin() + 3);
    const AimAssistResult r2 = aim_assist_pick(ts.data(), ts.size(), 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2],
                                               mouse_params(81920), occ, &ctx);
    SH_CHECK(r2.index == 0);
}

SH_TEST(aim_assist_occlusion_rejects_and_axis_skips_query) {
    AimAssistTarget t;
    t.x = 40960;
    t.z = 500;
    t.y_low = 0;
    t.y_high = 8192;
    t.radius = 1024;

    OccCtx ctx;
    ctx.block_all = true;
    SH_CHECK(aim_assist_pick(&t, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], mouse_params(81920), occ, &ctx)
                 .index == -1);

    // An enemy exactly at the camera's position has kmult 0 and is rejected
    // outright; the occlusion query is never reached.
    t.x = 0;
    t.z = 0;
    ctx.queries = 0;
    SH_CHECK(aim_assist_pick(&t, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], mouse_params(81920), occ, &ctx)
                 .index == -1);
    SH_CHECK(ctx.queries == 0);

    // No callback: nothing is occluded.
    t.x = 40960;
    t.z = 500;
    const AimAssistResult r2 =
        aim_assist_pick(&t, 1, 0, 4096, 0, kFwd[0], kFwd[1], kFwd[2], mouse_params(81920), nullptr, nullptr);
    SH_CHECK(r2.index == 0);
}

SH_TEST(aim_assist_vertical_forward_guard) {
    AimAssistTarget t;
    t.x = 0;
    t.z = 40960;
    t.y_low = 0;
    t.y_high = 8192;
    t.radius = 1024;

    OccCtx ctx;
    // Straight up: the horizontal divisor is zero, the picker returns nothing
    // instead of dividing by zero.
    SH_CHECK(aim_assist_pick(&t, 1, 0, 0, 0, 0, 4096, 0, mouse_params(81920), occ, &ctx).index == -1);
}
