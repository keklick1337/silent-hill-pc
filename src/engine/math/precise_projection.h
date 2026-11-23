// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Precise vertex projection of the engine (docs/RENDER.md section 5.3): one call projects a vertex both
// the way the game's geometry unit does (integer screen position, depth, view-space values, flags) and in double
// precision, and hands the second result back as an explicit record. The drawer takes the record and puts the
// precise screen position, the perspective weight and the view-space position into the packet it builds; nothing
// is keyed by the address where the integer result is stored.
//
// The record holds, per vertex:
//   sx, sy, w      the precise screen position and the perspective weight (0: no valid projection, draw affine)
//   view_x/y/z     the view-space position (the accumulators of the transform) for near-plane clipping and the
//                  per-pixel flashlight
//   ofx, ofy, h    the projection registers in force when the vertex was transformed (the game changes them in
//                  the middle of a frame, so each vertex carries its own)

#include "engine/math/fixed_geometry.h"

namespace sh::math {

// Unquantised camera, as latched by the caller when it set the rotation and translation: nine rotation values
// (row-major, 1.0 = identity) and three translation values in the units of the integer translation. A transform is
// used only when both parts are present.
struct ExactTransform {
    bool has_rotation = false;
    bool has_translation = false;
    double rot[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    double trans[3] = {0, 0, 0};
};

struct Vec3d {
    double x = 0, y = 0, z = 0;
};

struct PreciseOptions {
    bool unquantized_depth = true;  // w from the unclamped depth instead of the 16-bit depth register
    bool whole_map_far = false;     // far geometry projected from unclamped values (also changes the integer result)
};

struct PreciseVertex {
    Projected v;                    // the integer result, identical to project_vertex / project_vertex_far
    std::int32_t unclamped_depth = 0;  // only meaningful with whole_map_far
    float sx = 0, sy = 0, w = 0;
    float view_x = 0, view_y = 0, view_z = 0;
    float ofx = 0, ofy = 0, h = 0;
};

// `exact_camera` and `exact_vertex` may be null (the integer values are then the precise ones).
PreciseVertex project_precise(const Projection& p, const Vec3s& v, const Vec3d* exact_vertex,
                              const ExactTransform* exact_camera, const PreciseOptions& opt);

}  // namespace sh::math
