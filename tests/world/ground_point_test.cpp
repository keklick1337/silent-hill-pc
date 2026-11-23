// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include <cstring>
#include <span>

#include "engine/world/ground_point.h"
#include "engine/world/point_math.h"

namespace {

struct FakeGround : sh::world::GroundPointSource {
    int kind = 3;
    int calls = 0;
    sh::world::VoidDrop drop;
    void ground(std::int32_t x, std::int32_t z, std::int32_t* height, int* k) override {
        ++calls;
        *height = x + z;
        *k = kind;
    }
    sh::world::VoidDrop void_drop() const override { return drop; }
};

}  // namespace

SH_TEST(ground_point_remembers_the_last_point) {
    FakeGround g;
    sh::world::GroundPointCache cache;
    cache.bind(&g);
    SH_CHECK(cache.height(100, 200) == 300);
    SH_CHECK(cache.kind(100, 200) == 3);
    SH_CHECK(g.calls == 1);
    SH_CHECK(cache.height(101, 200) == 301);
    SH_CHECK(g.calls == 2);
}

SH_TEST(ground_point_no_ground_is_eight_metres_down) {
    FakeGround g;
    g.kind = 0;
    sh::world::GroundPointCache cache;
    cache.bind(&g);
    SH_CHECK(cache.height(5, -5) == 8 * 4096 && cache.kind(5, -5) == 0);
    g.kind = 12;
    SH_CHECK(cache.height(6, -5) == 8 * 4096 && cache.kind(6, -5) == 12);
}

SH_TEST(ground_point_drop_maps) {
    FakeGround g;
    g.kind = 0;
    // map5_s01's overlay.json: {"no_ground": true, "no_ground_max_z": 0.0}.
    g.drop = sh::world::VoidDrop{true, true, 0, false};
    sh::world::GroundPointCache cache;
    cache.bind(&g);
    SH_CHECK(cache.height(0, 0) == 4 * 4096 && cache.kind(0, 0) == 7);
    SH_CHECK(cache.height(0, 1) == 8 * 4096 && cache.kind(0, 1) == 0);
    g.kind = 12;
    SH_CHECK(cache.height(0, 2) == 8 * 4096 && cache.kind(0, 2) == 12);
    // map6_s00's: {"no_ground": true, "open_ground": true}.
    g.drop = sh::world::VoidDrop{true, false, 0, true};
    SH_CHECK(cache.height(0, 3) == 4 * 4096 && cache.kind(0, 3) == 7);
    g.kind = 0;
    SH_CHECK(cache.height(0, 4) == 4 * 4096 && cache.kind(0, 4) == 7);
}

SH_TEST(ground_point_minus_one_kind_is_asked_again) {
    FakeGround g;
    g.kind = -1;
    sh::world::GroundPointCache cache;
    cache.bind(&g);
    (void)cache.height(1, 1);
    (void)cache.height(1, 1);
    SH_CHECK(g.calls == 2);
}

SH_TEST(point_math_heading_and_elevation) {
    // Straight ahead along +Z on the level: heading 0, elevation a quarter turn from +Y.
    SH_CHECK(sh::world::heading_and_elevation({0, 0, 0}, {0, 0, 4096}) == (1024 << 16));
    // Along +X: heading a quarter turn.
    SH_CHECK((sh::world::heading_and_elevation({0, 0, 0}, {4096, 0, 0}) & 0xFFFF) == 1024);
    // Along -X: the heading is the negative angle as an unsigned 16-bit value.
    SH_CHECK((sh::world::heading_and_elevation({0, 0, 0}, {-4096, 0, 0}) & 0xFFFF) == 0x10000 - 1024);
    // Straight down (+Y): elevation 0.
    SH_CHECK(sh::world::heading_and_elevation({0, 0, 0}, {0, 4096, 0}) >> 16 == 0);
}

SH_TEST(point_math_mul_fixed_and_bone_points) {
    SH_CHECK(sh::world::mul_fixed(3 * 4096, 2 * 4096, 12) == 6 * 4096);
    SH_CHECK(sh::world::mul_fixed(-4096, 4096, 12) == -4096);
    SH_CHECK(sh::world::mul_fixed(0x40000000, 0x40000000, 30) == 0x40000000);
    sh::world::BoneFrame bone;  // identity rotation
    bone.translation = {256, -512, 0};
    const sh::math::Vec3s in[2] = {{0, 0, 0}, {128, 0, -256}};
    sh::world::WorldPoint out[2];
    sh::world::bone_points_to_world(bone, std::span(in), std::span(out));
    SH_CHECK(out[0].x == 4096 && out[0].y == -8192 && out[0].z == 0);
    SH_CHECK(out[1].x == 6144 && out[1].y == -8192 && out[1].z == -4096);
}
