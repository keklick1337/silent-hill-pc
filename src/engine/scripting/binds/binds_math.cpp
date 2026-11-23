// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Named binds of the game's integer math, so map scripts get the original's numbers:
//   math.segment_distance(px, pz, ax, az, bx, bz) -> distance, t: the point's distance from the segment a-b (or its
//       nearer end) and where along it the point lies (Q12, 0 at a, 4096 at b).
//   math.distance_2d(x1, z1, x2, z2): the planar distance of two Q12 points.
//   math.vector_length(x, y, z): the camera system's length of a Q12 vector.
//   math.bone_point(boneRef, x, y, z) -> x, y, z: the point (x, y, z) of the bone's space in the world, both in
//       geometry units (256 = 1 m); the bone's world matrix is brought up to date first.

#include "engine/scripting/binds/binds.h"

#include "engine/combat/combat_math.h"
#include "engine/scripting/binds/game_links.h"

namespace sh::script::binds {

namespace {

using named::Call;

int segment_distance(Call& c) {
    long long v[6];
    if (!c.ints(6, v)) {
        return -1;
    }
    std::int16_t along = 0;
    const std::uint32_t d = sh::combat::point_segment_distance(
        static_cast<std::int32_t>(v[0]), static_cast<std::int32_t>(v[1]), static_cast<std::int32_t>(v[2]),
        static_cast<std::int32_t>(v[3]), static_cast<std::int32_t>(v[4]), static_cast<std::int32_t>(v[5]), &along);
    return c.reply({static_cast<std::int32_t>(d), along});
}

int distance_2d(Call& c) {
    long long v[4];
    if (!c.ints(4, v)) {
        return -1;
    }
    const std::int32_t from[3] = {static_cast<std::int32_t>(v[0]), 0, static_cast<std::int32_t>(v[1])};
    const std::int32_t to[3] = {static_cast<std::int32_t>(v[2]), 0, static_cast<std::int32_t>(v[3])};
    return c.reply({sh::combat::planar_distance({from[0], from[1], from[2]}, {to[0], to[1], to[2]})});
}

int vector_length(Call& c) {
    if (c.argc() != 3 || !c.ints_at(0, 3)) {
        return c.fail("%s takes x, y, z", c.name());
    }
    return c.reply({Vc_VectorMagnitudeCalc(static_cast<std::int32_t>(c.integer(0)),
                                           static_cast<std::int32_t>(c.integer(1)),
                                           static_cast<std::int32_t>(c.integer(2)))});
}

int bone_point(Call& c) {
    if (c.argc() != 4 || !c.ints_at(1, 3)) {
        return c.fail("%s takes a bone reference and x, y, z", c.name());
    }
    if (c.is_nil_or_zero(0)) {
        return c.fail("%s: no bone", c.name());
    }
    if (!c.is_text(0)) {
        return c.fail("a reference argument takes a reference name or nil");
    }
    char err[160] = {};
    void* bone = PcScriptNamed_RefAddr(c.text(0), err, sizeof err);
    if (bone == nullptr) {
        return c.fail("%s", err);
    }
    std::int16_t m[3][3];
    std::int32_t t[3];
    sh::script::links::bone_world_matrix(bone, m, t);
    // The point is 16-bit in the original.
    const long long p[3] = {static_cast<std::int16_t>(c.integer(1)), static_cast<std::int16_t>(c.integer(2)),
                            static_cast<std::int16_t>(c.integer(3))};
    long long out[3];
    for (int row = 0; row < 3; ++row) {
        const long long sum = static_cast<long long>(t[row]) * 4096 + m[row][0] * p[0] + m[row][1] * p[1] +
                              m[row][2] * p[2];
        out[row] = static_cast<std::int32_t>(sum >> 12);
    }
    return c.reply({out[0], out[1], out[2]});
}

const named::Bind kBinds[] = {
    {"math.segment_distance", segment_distance},
    {"math.distance_2d", distance_2d},
    {"math.vector_length", vector_length},
    {"math.bone_point", bone_point},
};

}  // namespace

std::span<const named::Bind> math_binds() { return kBinds; }

}  // namespace sh::script::binds
