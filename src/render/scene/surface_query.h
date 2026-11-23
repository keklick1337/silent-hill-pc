#pragma once

// What a ray meets in the drawn world: the visible surface (the cells and world objects of the last world frame), not
// the collision. The collision has neither normals nor textures; a mark left on the world (render/decals) needs both.

#include <array>
#include <optional>
#include <vector>

namespace sh::scene {

using SurfaceVec = std::array<double, 3>;  // world, PS1 axes, 256 per metre

struct SurfaceHit {
    SurfaceVec point{};
    SurfaceVec normal{};  // unit, toward the start of the segment
    // The triangles of the hit surface around the point: same plane and facing, within `patch_radius` of it.
    std::vector<std::array<SurfaceVec, 3>> patch;
};

// The first solid point of the drawn world on the segment. A triangle is passed through where its texture is cut out
// (alpha 0) or, on a semi-transparent material, blended (the STP texels: glass). Empty when the segment meets nothing
// solid.
std::optional<SurfaceHit> solid_surface_on_segment(const SurfaceVec& from, const SurfaceVec& to, double patch_radius);

}  // namespace sh::scene
