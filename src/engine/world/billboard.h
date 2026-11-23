// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Camera-facing sprites of the world (trees, branches, lens-flare discs): the lighting of the four corners, the
// projection of one sprite point and the size of its quad (docs/RENDER.md section 5.3). Integer code on the
// engine's fixed-point math (fixed_geometry.h); the game keeps the packets and the ordering table.

#include <array>
#include <cstdint>

#include "engine/math/fixed_geometry.h"

namespace sh::world {

// ---- Corner colours ---------------------------------------------------------------------------------------

struct BillboardLightIn {
    int mode = 0;                       // world light mode: 0 flat tint, 1 and above lit by the light matrix
    math::Rgb tint;                      // world tint colour
    std::int32_t map_light = 0;         // map lighting scale, Q12
    math::Matrix3 light;                 // light matrix after the mode's set-up (only its first row is used)
    math::Vec3i background;              // background colour after the set-up
    math::Matrix3 color_matrix;          // light colour matrix
};

struct BillboardLightOut {
    std::array<math::Rgb, 4> corners;    // top, right, left, bottom
    math::Matrix3 light;                 // the light matrix as it was used (rows rebuilt from the first)
    math::Vec3i background;              // the background colour as it was used (scaled in mode 1)
};

BillboardLightOut billboard_light(const BillboardLightIn& in);

// ---- One sprite point -------------------------------------------------------------------------------------

struct BillboardPoint {
    std::int16_t x = 0, y = 0, z = 0;   // view-space offset of the sprite's foot, Q12
    std::int16_t half_width = 0;        // Q12 at the reference distance
    std::int16_t height = 0;
};

struct BillboardView {
    math::Projection projection;         // world to screen of the sprite group
    bool whole_map_far = false;         // far projection for the whole-map mode
    bool whole_map_draw = false;        // the whole-map mode is drawing (far sprites keep their true depth)
    std::int32_t last_depth = 0;        // true depth of the last far projection (in/out)
    std::int32_t depth_limit = 0;       // sprites at or past this depth are not drawn
};

struct BillboardQuad {
    bool visible = false;
    std::array<std::int16_t, 4> x{}, y{};   // top, right, left, bottom
    std::int32_t depth = 0;                 // depth / 4, the ordering-table key before the shift
    std::int32_t full_depth = 0;            // the depth the key stands for (true depth for far sprites)
};

BillboardQuad billboard_quad(BillboardView& view, const BillboardPoint& point);

// Distance past which a sprite is not drawn: the scaled base distance, cut by the fog's far distance when fog is on,
// unlimited in the whole-map mode.
std::int32_t billboard_depth_limit(std::int32_t scaled_base, bool fog_enabled, std::int32_t fog_far, bool whole_map_draw);

// Fog amount of a sprite, 0..127: ramp value (0..255) and the fog's intensity added, capped at Q12(1).
std::uint8_t billboard_fog_byte(std::int32_t ramp_u8, std::int32_t intensity);

}  // namespace sh::world
