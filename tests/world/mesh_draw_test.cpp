// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/world/mesh_draw.h"

using namespace sh::world;

SH_TEST(mesh_far_limit_cases) {
    SH_CHECK(mesh_far_base(0x79C, 0) == 0x79C);
    SH_CHECK(mesh_far_base(0x79C, 200) == 0xF38);
    SH_CHECK(mesh_far_limit(100, 0, false, 10, false) == 0x79C << 2);
    SH_CHECK(mesh_far_limit(100, 1, false, 10, false) == 0x79C << 3);
    SH_CHECK(mesh_far_limit(100, 0, true, 500, false) == 500);
    SH_CHECK(mesh_far_limit(100, 0, true, 99999, false) == 0x79C << 2);
    SH_CHECK(mesh_far_limit(100, 0, true, 500, true) == 0x7FFFFFFF);
}

SH_TEST(mesh_visible_half_width_cases) {
    // No window: PS1 half-width plus margin, floored at the half-width.
    SH_CHECK(mesh_visible_half_width(320, 240, 0, 0, 1.0f, 1.0f) == 224);
    // 16:9 window: 240*1920/(2*1080)=213.33 -> +64.5 = 277.
    SH_CHECK(mesh_visible_half_width(320, 240, 1920, 1080, 1.0f, 1.0f) == 277);
    // A narrow hscale below 0.01 is ignored.
    SH_CHECK(mesh_visible_half_width(320, 240, 0, 0, 1.0f, 0.0f) == 224);
}

SH_TEST(mesh_ot_clamps) {
    SH_CHECK(ot_last_depth(2048, 0, 1) == 2047 << 3);
    SH_CHECK(ot_clamp_depth(100, 2048, 0, 0) == 100);
    SH_CHECK(ot_clamp_depth(2048 * 4, 2048, 0, 0) == 2047 << 2);
    SH_CHECK(ot_clamp_depth(2048 * 4, 2048, 10, 0) == 2037 << 2);
    SH_CHECK(whole_map_depth_rescue(-5, 2048, 0, 0, true) == 2047 << 2);
    SH_CHECK(whole_map_depth_rescue(-5, 2048, 0, 0, false) == -5);
    SH_CHECK(whole_map_depth_rescue(0, 2048, 0, 0, true) == 0);
}

SH_TEST(mesh_draw_options_parse) {
    auto ok = MeshDrawOptions::parse(R"({"format":"sh-mesh-draw","msaa_samples":4,"smooth_shading":true})");
    SH_CHECK(ok);
    SH_CHECK(ok.value().msaa_samples == 4 && ok.value().smooth_shading && ok.value().vertex_snap);
    SH_CHECK(!MeshDrawOptions::parse(R"({"format":"sh-mesh-draw","msaa_samples":3})"));
    SH_CHECK(!MeshDrawOptions::parse(R"({"format":"x"})"));
}
