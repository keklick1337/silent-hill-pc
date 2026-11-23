// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Per-mesh set-up of the model drawing (docs/RENDER.md section 5.3): copying the mesh vertices into the
// scratch pools, the light of the normals (static light, and the dynamic light that falls off with distance), the
// colour of character normals and the character light set-up. Integer code on the engine's fixed-point math
// (fixed_geometry.h) over plain arrays; the game keeps the scratch buffer, the geometry state it still hands to the
// packet drawers, and the packets.

#include <cstdint>

#include "engine/math/fixed_geometry.h"

namespace sh::world {

struct ScreenXy {
    std::int16_t x = 0, y = 0;
};

// Copies a mesh's vertices into the scratch pools: `count` screen positions, the depths and the normal-slot bytes.
// The unpadded form copies exactly `count` entries (a model placed at an offset); the padded form copies the depths
// in pairs and the slot bytes in fours, so it reads and writes up to one and three entries past the end, like the
// game's word-wide loops.
void copy_vertices(const ScreenXy* in_xy, const std::int16_t* in_z, const std::uint8_t* in_slots, int vertex_count,
                   int slot_count, bool padded, ScreenXy* out_xy, std::int16_t* out_z, std::uint8_t* out_slots);

// One normal of a mesh: three signed bytes and the number of vertex slots it lights.
struct MeshNormal {
    std::int8_t nx = 0, ny = 0, nz = 0;
    std::uint8_t count = 0;
};

// Static light: every normal gets the light of its direction against the light vector (first light matrix row), plus
// the map light, as a shade byte for each of its vertex slots. `shade` is the slot array, written in place.
void light_normals_static(const std::int16_t light_vec[3], const MeshNormal* normals, int normal_count,
                          std::int32_t map_light, std::uint8_t* shade);

// Dynamic light: the vertex slots hold vertex indices on entry and shade bytes on return. For each normal the
// vertices are projected with the light vector, the normal and the vertex offset from `eye` as the three matrix rows
// (a distance-weighted brightness of the vertex). A normal with a zero count still uses (and overwrites) one slot.
struct DynamicLightIn {
    std::int16_t light_vec[3] = {};
    std::int16_t eye[3] = {};              // offset subtracted from every vertex
    std::int32_t map_light = 0;            // world map light, Q12 (its 1/32 is added)
    std::uint8_t light_cap = 0;            // the environment light, the highest shade
    std::int16_t depth_cue_slope = 0;      // coprocessor depth cue registers at the call
    std::int32_t depth_cue_base = 0;
};
struct DynamicLightOut {
    int slots_used = 0;
    bool any = false;                      // at least one normal was processed
    math::Matrix3 last_rot;                 // the matrix the last probe left in the rotation registers
};
DynamicLightOut light_normals_dynamic(const DynamicLightIn& in, const MeshNormal* normals, int normal_count,
                          const ScreenXy* vertex_xy, const std::int16_t* vertex_z, std::uint8_t* slots);

// The three outputs of the character light set-up.
struct CharaLight {
    std::int16_t rot[3][3] = {};           // scratch matrix kept for later users
    ScreenXy screen;                       // projected light direction (+1024 each, as the scratch holds it)
    std::int32_t depth_cue = 0;
    std::int16_t source[3] = {};           // light vector registers
    std::int32_t back[3] = {};             // background colour
};

struct CharaLightIn {
    std::int16_t light_dir[3] = {};        // world light direction (the point light's matrix vector, Q8)
    std::int16_t light_pos[3] = {};        // world light position (the point light's offset)
    std::uint8_t light_cap = 0;            // the world light's highest light level
    std::uint8_t env_color[3] = {};        // the world's character light colour
    std::uint8_t tint[3] = {};             // world tint colour
    std::int32_t alpha = 0;                // Q12
    std::int16_t depth_cue_slope = 0;
    std::int32_t depth_cue_base = 0;
};

// Light direction rows, light vector and background colour from the world light for a character of opacity `alpha`.
CharaLight chara_light(const CharaLightIn& in);

// Background colour and light vector scaled by `scale` (Q12) from the world light.
struct ScaledLight {
    std::int16_t source[3] = {};
    std::int32_t back[3] = {};
};
ScaledLight scaled_light(const std::int16_t light_dir[3], const std::uint8_t env_color[3], std::int32_t scale);

// Ambient colour: the world tint blended with black by (1 - alpha * map_light), Q12. `far_color` is the coprocessor's
// far colour register triple (raw); the blend goes toward it.
math::Rgb ambient_color(const std::uint8_t tint[3], std::int32_t map_light, std::int32_t alpha,
                       const math::Vec3i& far_color);

}  // namespace sh::world
