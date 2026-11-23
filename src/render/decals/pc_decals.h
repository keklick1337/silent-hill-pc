// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Bullet-hole decals (PC only, config key `bullet_decals`).
//
// When a player firearm ray ends on world geometry (not a character), the combat module hands the hit point, the
// bullet direction and the ray's start here. The mark is laid on the drawn surface found along the shot at that point
// (render/scene/surface_query.h): turned by its normal, cut to it where the surface ends under the mark, and left out
// where the bullet met a cut-out or glass texel. Decals stay until the
// map changes, at most 64 (the oldest is overwritten). The picture is made by the engine (bullet_hole.h): a sheet of
// sixteen holes on a sheet of the replacement pictures; the hit point picks the hole, its turn and its size.
//
// Each frame the decals are drawn as quads of the engine's world-space effect list (render/draw3d) with their own
// view depth, so the scene in front hides them; the primitive shader's decal program lights the hole's relief with
// the flashlight and fogs and darkens it like the wall under it.

#include <cstdint>

namespace sh::render {

// Q12 world point or vector.
struct DecalPoint {
    std::int32_t x = 0, y = 0, z = 0;
};

// Records a bullet impact on world geometry: `hit` is the clipped hit point, `direction` the bullet direction
// (unnormalised), `origin` the ray's start (the hand).
void bullet_decal_add(const DecalPoint& hit, const DecalPoint& direction, const DecalPoint& origin);

}  // namespace sh::render

// Entry points the world drawer calls (C linkage).
extern "C" {
// Projects and links every live decal into world ordering table `ot`, once per frame.
void Pc_DecalsDraw(void* ot);
// Clears the decals and marks the sheet for putting again.
void Pc_DecalsReset(void);
}
