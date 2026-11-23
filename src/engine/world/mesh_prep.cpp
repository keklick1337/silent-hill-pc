// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/mesh_prep.h"

#include <algorithm>

#include "engine/world/fixed_math.h"

namespace sh::world {

namespace {

using std::int16_t;
using std::int32_t;
using std::uint8_t;

// Projection of the light brightness probes: unit screen distance and a screen offset that puts the origin at the
// corner of the 11-bit screen box (-1024), so the result is a blend of the two screen coordinates and the depth cue.
math::Projection probe_projection(const math::Matrix3& rot, int16_t slope, int32_t base) {
    math::Projection p;
    p.rot = rot;
    p.trans = {};
    p.h = 16;
    p.ofx = -1024 * 65536;
    p.ofy = -1024 * 65536;
    p.dqa = slope;
    p.dqb = base;
    return p;
}

struct Probe {
    int32_t x = 0, y = 0, depth = 0;  // screen position moved to 0..2047, depth cue 0..0x1000
};

Probe probe(const math::Projection& p, const math::Vec3s& v) {
    const math::Projected r = math::project_vertex(p, v);
    const std::int16_t cue = math::depth_cue(p, r.h_over_sz);
    return {r.sx + 1024, r.sy + 1024, cue};
}

// The distance-weighted brightness of one probe, before the map light is added.
int32_t probe_brightness(const Probe& pr, int32_t* near_boost) {
    int32_t a = ((pr.x * pr.y) + (pr.y * (pr.depth >> 4))) >> 5;
    a -= 16;
    if (a < 0) {
        a = 0;
    }
    int32_t v = (pr.x >> 1) + (pr.depth >> 2);
    if (v > 48) {
        v = 48;
    }
    *near_boost = v;
    return a;
}

int16_t shift5(int8_t n) {
    return static_cast<int16_t>(static_cast<int32_t>(n) * 32);
}

int16_t wrap16(int32_t v) {
    return static_cast<int16_t>(v);
}

}  // namespace

void copy_vertices(const ScreenXy* in_xy, const int16_t* in_z, const uint8_t* in_slots, int vertex_count,
                   int slot_count, bool padded, ScreenXy* out_xy, int16_t* out_z, uint8_t* out_slots) {
    const int z_count = padded ? (vertex_count + 1) & ~1 : vertex_count;
    const int slot_bytes = padded ? (slot_count + 3) & ~3 : slot_count;
    for (int i = 0; i < vertex_count; ++i) {
        out_xy[i] = in_xy[i];
    }
    for (int i = 0; i < z_count; ++i) {
        out_z[i] = in_z[i];
    }
    for (int i = 0; i < slot_bytes; ++i) {
        out_slots[i] = in_slots[i];
    }
}

void light_normals_static(const int16_t light_vec[3], const MeshNormal* normals, int normal_count, int32_t map_light,
                          uint8_t* shade) {
    math::Matrix3 light;
    light.m[0][0] = light_vec[0];
    light.m[0][1] = light_vec[1];
    light.m[0][2] = light_vec[2];
    for (int r = 1; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            light.m[r][c] = 0;
        }
    }
    uint8_t* slot = shade;
    for (int i = 0; i < normal_count; ++i) {
        const MeshNormal& n = normals[i];
        const math::MatVec mv = math::matrix_vector(light, {shift5(n.nx), shift5(n.ny), shift5(n.nz)}, math::Offset::None, {}, true);
        int32_t v = mv.saturated.x;
        v += map_light;
        v >>= 5;
        if (v > 0xFF) {
            v = 0xFF;
        }
        for (int k = 0; k < n.count; ++k) {
            *slot++ = static_cast<uint8_t>(v);
        }
    }
}

DynamicLightOut light_normals_dynamic(const DynamicLightIn& in, const MeshNormal* normals, int normal_count,
                                      const ScreenXy* vertex_xy, const int16_t* vertex_z, uint8_t* slots) {
    DynamicLightOut out;
    const int32_t map_part = in.map_light >> 5;
    const int32_t cap = in.light_cap;
    uint8_t* slot = slots;
    for (int i = 0; i < normal_count; ++i) {
        const MeshNormal& n = normals[i];
        math::Matrix3 rot;
        rot.m[0][0] = in.light_vec[0];
        rot.m[0][1] = in.light_vec[1];
        rot.m[0][2] = in.light_vec[2];
        rot.m[1][0] = shift5(n.nx);
        rot.m[1][1] = shift5(n.ny);
        rot.m[1][2] = shift5(n.nz);
        // A group with no vertices still handles one slot.
        const int count = std::max<int>(n.count, 1);
        for (int k = 0; k < count; ++k) {
            const int idx = slot[k];
            rot.m[2][0] = wrap16(vertex_xy[idx].x - in.eye[0]);
            rot.m[2][1] = wrap16(vertex_xy[idx].y - in.eye[1]);
            rot.m[2][2] = wrap16(vertex_z[idx] - in.eye[2]);
            const math::Projection p = probe_projection(rot, in.depth_cue_slope, in.depth_cue_base);
            const Probe pr = probe(p, {rot.m[2][0], rot.m[2][1], rot.m[2][2]});
            int32_t boost = 0;
            int32_t a = probe_brightness(pr, &boost);
            a += map_part;
            a += boost;
            if (cap < a) {
                a = cap;
            }
            slot[k] = static_cast<uint8_t>(a);
            out.last_rot = rot;
            out.any = true;
        }
        slot += count;
    }
    out.slots_used = static_cast<int>(slot - slots);
    return out;
}

CharaLight chara_light(const CharaLightIn& in) {
    CharaLight out;
    const int32_t px = in.light_pos[0], py = in.light_pos[1], pz = in.light_pos[2];
    int32_t len = game_sqrt(wrap_add(wrap_add(wrap_mul(px, px), wrap_mul(py, py)), wrap_mul(pz, pz)));
    if (len == 0) {
        len = 1;
    }
    const int32_t nx = -px, ny = -py, nz = -pz;
    out.rot[0][0] = in.light_dir[0];
    out.rot[0][1] = in.light_dir[1];
    out.rot[0][2] = in.light_dir[2];
    out.rot[1][0] = wrap16(wrap_mul(nx, 4096) / len);
    out.rot[1][1] = wrap16(wrap_mul(ny, 4096) / len);
    out.rot[1][2] = wrap16(wrap_mul(nz, 4096) / len);
    out.rot[2][0] = wrap16(nx);
    out.rot[2][1] = wrap16(ny);
    out.rot[2][2] = wrap16(nz);

    math::Matrix3 rot;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            rot.m[r][c] = out.rot[r][c];
        }
    }
    const Probe pr = probe(probe_projection(rot, in.depth_cue_slope, in.depth_cue_base),
                           {out.rot[2][0], out.rot[2][1], out.rot[2][2]});
    out.screen = {wrap16(pr.x), wrap16(pr.y)};
    out.depth_cue = pr.depth;

    int32_t boost = 0;
    int32_t level = probe_brightness(pr, &boost);
    level += boost;
    level >>= 1;
    if (in.light_cap < level) {
        level = in.light_cap;
    }
    level = mul_q12(level, in.alpha);
    if (level < 0) {
        level = 0;
    }
    const int32_t dim = (level >> 2) > 40 ? level - 40 : level - (level >> 2);
    constexpr int32_t kLimit = 5734;  // Q12 1.4, truncated
    for (int k = 0; k < 3; ++k) {
        int32_t v = (out.rot[1][k] * dim) >> 7;
        out.source[k] = static_cast<int16_t>(std::clamp(v, -kLimit, kLimit));
    }

    int32_t mix = 40;
    if ((level >> 2) <= 40) {
        mix = level >> 2;
    }
    if (mix > 64) {
        mix = 64;
    }
    for (int k = 0; k < 3; ++k) {
        out.back[k] = mul_q12(in.env_color[k] + ((in.tint[k] * mix) >> 7), in.alpha);
    }
    return out;
}

ScaledLight scaled_light(const int16_t light_dir[3], const uint8_t env_color[3], int32_t scale) {
    ScaledLight out;
    for (int k = 0; k < 3; ++k) {
        out.source[k] = static_cast<int16_t>(mul_q12(light_dir[k], scale) >> 1);
        out.back[k] = mul_q12(env_color[k], scale);
    }
    return out;
}

math::Rgb ambient_color(const uint8_t tint[3], int32_t map_light, int32_t alpha, const math::Vec3i& far_color) {
    const int32_t inverse = 4096 - mul_q12(alpha, map_light);
    return math::dpcs({tint[0], tint[1], tint[2]}, static_cast<int16_t>(inverse), far_color).rgb;
}

}  // namespace sh::world
