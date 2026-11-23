// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Fixed-point 3D math of the game (docs/RENDER.md section 5.3): the perspective projection of vertices,
// matrix times vector with an optional offset, the depth-cue colour blends and the rotation-matrix builders. Plain
// integer C++ on engine values: every function takes its inputs explicitly and returns its outputs. The results
// match the original's geometry coprocessor bit for bit (its 12-bit product shift, its 16-bit saturation, its
// reciprocal-table divide), which the replay depends on (tests/world/fixed_geometry_test.cpp pins them).
//
// Products of a Q12 matrix and a vector are shifted back by 12. Intermediate vectors saturate to 16 bits (signed;
// `clamp_to_zero` makes the lower limit 0 instead of -0x8000, as the lit-colour stages do).

#include <array>
#include <cstdint>

namespace sh::math {

struct Vec3s {
    std::int16_t x = 0, y = 0, z = 0;
};
struct Vec3i {
    std::int32_t x = 0, y = 0, z = 0;
};

// Rotation (or light, or colour) matrix, row-major: m[row][column], Q12.
struct Matrix3 {
    std::int16_t m[3][3] = {{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}};
};

// A rotation with a translation (the game's matrix record).
struct Transform {
    Matrix3 rot;
    Vec3i trans;
};

// Unsigned reciprocal divide (numerator * 65536 / denominator, roughly) of the projection: 0xffffffff on overflow.
std::uint32_t divide(std::uint16_t numerator, std::uint16_t denominator);

// ---- Projection -------------------------------------------------------------------------------------------

// What projecting a vertex reads: the view (Q12 rotation, translation added before the product is shifted back),
// the screen distance, the screen offset (Q16) and the depth-cue line.
struct Projection {
    Matrix3 rot;
    Vec3i trans;
    std::int16_t h = 0;        // distance to the screen
    std::int32_t ofx = 0;      // screen offset, Q16
    std::int32_t ofy = 0;
    std::int16_t dqa = 0;      // depth cue slope
    std::int32_t dqb = 0;      // depth cue base
};

struct Projected {
    Vec3i view;                // the vertex in view space (32 bits)
    Vec3s view_short;          // the same, saturated to 16 bits (what the divide uses)
    std::uint16_t sz = 0;      // depth, saturated to 0..0xffff
    std::int16_t sx = 0;       // screen position, saturated to -0x400..0x3ff
    std::int16_t sy = 0;
    std::uint32_t h_over_sz = 0;  // perspective scale, saturated to 0x1ffff
    bool too_near = false;     // the depth was under half the screen distance: the scale saturated
    bool depth_clamped = false;  // the depth left 0..0xffff (behind the eye or too far)
};

// One vertex: transform by the matrix and translation, divide by depth, add the screen offset.
Projected project_vertex(const Projection& p, const Vec3s& v);

// Depth cue (0..0x1000) of a projected vertex from its perspective scale (`h_over_sz` of project_vertex).
std::int16_t depth_cue(const Projection& p, std::uint32_t h_over_sz);

// project_vertex as the whole-map far mode computes it: where the saturated depth or the saturated view x/y froze
// the projection, the screen position is recomputed in double precision from the unsaturated values.
// `unclamped_depth` is the true depth (0 .. 0x7fffffff).
struct ProjectedFar {
    Projected v;
    std::int32_t unclamped_depth = 0;
};
ProjectedFar project_vertex_far(const Projection& p, const Vec3s& v);

// The screen word of a projected vertex (x | y << 16), as the game's primitives store it.
inline std::uint32_t screen_word(const Projected& v) {
    return static_cast<std::uint32_t>(static_cast<std::uint16_t>(v.sx)) |
           (static_cast<std::uint32_t>(static_cast<std::uint16_t>(v.sy)) << 16);
}

// ---- Matrix times vector ----------------------------------------------------------------------------------

// What is added to the product: nothing, or a translation (in the units of the result).
enum class Offset { None, Translation };

struct MatVec {
    Vec3i full;                // 32-bit result
    Vec3s saturated;           // the same saturated to 16 bits
};
// m * v + offset, shifted back by 12.
MatVec matrix_vector(const Matrix3& m, const Vec3s& v, Offset o = Offset::None, const Vec3i& offset = {},
                     bool clamp_to_zero = false);

// ---- Colour blends ----------------------------------------------------------------------------------------

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};
struct ColorOut {
    Vec3i full;                // the blended colour, Q4 per component
    Vec3s saturated;
    Rgb rgb;                   // full >> 4, saturated to 0..255
};

// Depth cue of one colour: colour * 16 blended towards `far_color` by `cue` (0..0x1000).
ColorOut dpcs(const Rgb& color, std::int16_t cue, const Vec3i& far_color);
// Depth cue of a colour multiplied by an intensity vector: colour * intensity blended towards `far_color`.
ColorOut dpcl(const Rgb& color, const Vec3s& intensity, std::int16_t cue, const Vec3i& far_color);
// intensity * scale (Q12), as a colour.
ColorOut gpf(const Vec3s& intensity, std::int16_t scale);

// Lit colour of a normal without the surface colour: the light matrix times the normal, then the colour matrix times
// that plus the background colour.
ColorOut normal_color(const Matrix3& light, const Matrix3& color_matrix, const Vec3i& background, const Vec3s& normal,
                      bool clamp_to_zero);

// ---- Matrix functions -------------------------------------------------------------------------------------

// a * b, each column of b through the matrix-vector product (shifted by 12, saturated to 16 bits).
Matrix3 mul_matrix(const Matrix3& a, const Matrix3& b);
// Composition of two transforms: rot = a.rot * b.rot, trans = a.rot * (b.trans cut to 16 bits) + a.trans.
Transform comp_matrix(const Transform& a, const Transform& b);
// A 32-bit vector through the rotation: split into a high and a low part (15 bits each) so the 16-bit product holds
// it, both parts rotated, the low one scaled back by 8 and added.
Vec3i apply_matrix_lv(const Matrix3& m, const Vec3i& v);
// Rows scaled by the Q12 components of `v`.
Matrix3 scale_matrix(const Matrix3& m, const Vec3i& v);

// ---- Rotation-matrix builders -----------------------------------------------------------------------------
// Angles are Q12 where 4096 is a full turn; the sine and cosine are evaluated in double precision and truncated to
// Q12 per element (the game's runtime formula).

Matrix3 rot_matrix_zxy_neg(const Vec3s& rot);   // Ry * Rx * Rz in the camera convention
Matrix3 rot_matrix_xyz(const Vec3s& rot);       // Rz * Ry * Rx
Matrix3 rot_matrix_zxy(const Vec3s& rot);       // the transpose layout of rot_matrix_zxy_neg
Matrix3 rot_matrix_z(std::int32_t angle);

// ---- Vector helpers of the view code ----------------------------------------------------------------------

// Unit vector (Q12) of v and, optionally, the exact squared length (64 bits). The root is the game's 32-bit square
// root of the truncated sum; a zero root gives the zero vector.
Vec3i vector_normal(const Vec3i& v, std::int64_t* squared_length = nullptr);
// Cross product, each component shifted down by 12.
Vec3i outer_product12(const Vec3i& a, const Vec3i& b);

// Azimuth and altitude (Q12 angles, 0..4095) of the turn that takes the direction `from` to the direction `to`.
struct RelativeRotation {
    std::int32_t azimuth = 0;
    std::int32_t altitude = 0;
};
RelativeRotation relative_rotation(const Vec3i& from, const Vec3i& to);

}  // namespace sh::math
