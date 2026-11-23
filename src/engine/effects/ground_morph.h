// SPDX-License-Identifier: GPL-3.0-or-later
// The ground of map6_s00 turning into the Otherworld's: a 17 x 17
// grid of points over a fixed patch of the street, each with its own band of the morph level from the map's
// morph table. As the level passes a point's band the point goes through four looks (0 street .. 3 Otherworld);
// every cell of the grid is drawn once per look its corners show, cross-fading between neighbouring looks, from
// the loading-screen image's texture pages. The map script calls it every frame with the level.
//
//  The game gives the view, the
// projection, the lighting of a point and the texture placement (GroundMorphHost).

#pragma once

#include <array>
#include <cstdint>

#include "engine/effects/effect_prims.h"
#include "render/draw3d/draw3d.h"

namespace sh::effects {

constexpr int kGroundMorphGrid = 17;

// One point's band of the morph level (the map's table stores [column][row]).
struct GroundMorphBand {
    std::int16_t start = 0;  // level / 4 where the point starts to change
    std::int16_t end = 0;    // level / 4 where it is fully the Otherworld's
};
using GroundMorphTable = std::array<std::array<GroundMorphBand, kGroundMorphGrid>, kGroundMorphGrid>;

class GroundMorphHost {
public:
    virtual ~GroundMorphHost() = default;

    virtual Vec3 player_position() = 0;  // Q12
    // Sets the view for the frame at `origin` (whole metres, as Q12) and returns the distance to the projection
    // plane.
    virtual std::int32_t begin_view(const Vec3& origin) = 0;
    // Three points at once and one alone, like the game's projection takes them; depth is the raw screen z.
    virtual void project3(const Vec3s v[3], ScreenPoint out[3]) = 0;
    virtual ScreenPoint project(const Vec3s& v) = 0;
    // The colour the world's light and depth fade give a point at `pos` (geometry units) with `normal` (Q12)
    // under a world matrix that is the identity rotation moved by `offset`.
    virtual Rgb light(const Vec3s& pos, const Vec3s& normal, const Vec3& offset) = 0;
    // Palette word of look `look` (the loading-screen image's palette rows).
    virtual std::uint16_t palette(int look) = 0;
    virtual void emit(const draw3d::Polygon& polygon, std::int32_t bucket) = 0;
};

class GroundMorph {
public:
    static void draw(GroundMorphHost& host, const GroundMorphTable& table, std::int32_t level);
};

}  // namespace sh::effects
