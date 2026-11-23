// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The geometry state the game's view code and its drawers share between calls (docs/RENDER.md section 5.3):
// the view loaded for projection (rotation and translation), the light and light-colour matrices, the background and
// far colours, the screen (distance, offset, depth cue), a view stack, and the history of the last projections. The
// original kept all of this in its geometry coprocessor between operations; here it is one engine object with named
// members, and every operation that reads it is a method.
//
// Loading a rotation or translation from a record also keeps the precise-capture latch (precise_capture.h) in step:
// a matrix is latched by the address of its record, as the precise path keys its unquantised twins.
//
// The pure arithmetic is fixed_geometry.h; drawers that own their view use ViewProjection (view_projection.h) instead.

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/math/fixed_geometry.h"
#include "engine/math/view_projection.h"

namespace sh::math {

// The game's records seen as the engine's: a matrix record has the memory layout of Transform (nine Q12 shorts,
// then three 32-bit words), a vector record has members vx, vy, vz.
template <class Record>
Transform& transform_of(Record& record) {
    static_assert(sizeof(Record) == sizeof(Transform) && alignof(Record) == alignof(Transform),
                  "a matrix record is laid out as Transform");
    return *reinterpret_cast<Transform*>(&record);
}
template <class Record>
const Transform& transform_of(const Record& record) {
    static_assert(sizeof(Record) == sizeof(Transform) && alignof(Record) == alignof(Transform),
                  "a matrix record is laid out as Transform");
    return *reinterpret_cast<const Transform*>(&record);
}
// A bare 3x3 record (nine Q12 shorts).
inline Matrix3& matrix_of(std::int16_t (&rows)[3][3]) { return *reinterpret_cast<Matrix3*>(&rows); }
inline const Matrix3& matrix_of(const std::int16_t (&rows)[3][3]) {
    return *reinterpret_cast<const Matrix3*>(&rows);
}
template <class Vector>
Vec3s short_vector(const Vector& v) {
    return {static_cast<std::int16_t>(v.vx), static_cast<std::int16_t>(v.vy), static_cast<std::int16_t>(v.vz)};
}
template <class Vector>
Vec3i long_vector(const Vector& v) {
    return {static_cast<std::int32_t>(v.vx), static_cast<std::int32_t>(v.vy), static_cast<std::int32_t>(v.vz)};
}
template <class Vector, class Source>
void store_vector(Vector& out, const Source& v) {
    out.vx = v.x;
    out.vy = v.y;
    out.vz = v.z;
}

// The state starts all zero, as the original's did at power-on.
inline constexpr Matrix3 kZeroMatrix{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};

class GeometryState {
public:
    // ---- Screen ----
    // Power-on state of the game's geometry setup: screen distance 1000, depth cue -98/340, offset 0.
    void reset();
    // Screen centre in pixels (kept Q16) and the distance to the screen (16 bits).
    void set_screen_offset(std::int32_t x, std::int32_t y);
    void set_screen_distance(std::int32_t h) { screen_.h = static_cast<std::int16_t>(h); }
    std::int32_t screen_distance() const { return screen_.h; }
    std::int32_t screen_offset_x() const { return screen_.ofx >> 16; }
    std::int32_t screen_offset_y() const { return screen_.ofy >> 16; }
    // Depth cue slope (16 bits) and base.
    void set_depth_cue_slope(std::int32_t dqa) { screen_.dqa = static_cast<std::int16_t>(dqa); }
    void set_depth_cue_base(std::int32_t dqb) { screen_.dqb = dqb; }
    const ScreenSetup& screen() const { return screen_; }
    // Everything a projection reads: rotation, translation, screen distance, offset (Q16) and depth cue.
    Projection projection() const;

    // ---- View, lights and colours ----
    // Rotation and translation from a record in memory (latched for the precise path by the record's address).
    void load_rotation(const Matrix3& record);
    void load_rotation(const Transform& record) { load_rotation(record.rot); }
    void load_translation(const Transform& record);
    // The rotation transposed (rotates view vectors back into the model's frame); not latched.
    void load_rotation_transposed(const Matrix3& m);
    // Plain stores, not latched.
    void set_rotation(const Matrix3& m) { view_.rot = m; }
    void set_translation(const Vec3i& t) { view_.trans = t; }
    const Matrix3& rotation() const { return view_.rot; }
    const Vec3i& translation() const { return view_.trans; }
    const Transform& view() const { return view_; }

    void load_light_matrix(const Matrix3& m) { light_ = m; }
    // A light matrix whose first row is `direction` and the rest zero (one directional light).
    void load_light_direction(const Vec3s& direction);
    const Matrix3& light_matrix() const { return light_; }
    void load_color_matrix(const Matrix3& m) { light_color_ = m; }
    // Background colour: raw values, or 8-bit colour components (kept scaled by 16).
    void set_background(const Vec3i& bk) { background_ = bk; }
    void set_background_color(std::int32_t r, std::int32_t g, std::int32_t b) { background_ = {r << 4, g << 4, b << 4}; }
    const Vec3i& background() const { return background_; }
    void set_far_color(std::int32_t r, std::int32_t g, std::int32_t b) { far_color_ = {r << 4, g << 4, b << 4}; }
    const Vec3i& far_color() const { return far_color_; }

    // ---- View stack (20 entries, rotation and translation) ----
    // A full stack or an empty one logs and does nothing, as the game's library did.
    void push_view();
    void pop_view();

    // ---- Projection ----
    // One vertex through the loaded view and screen: writes the packed screen word (x | y << 16) to `screen_word`
    // and returns the ordering depth (depth / 4).
    std::int32_t project(const Vec3s& v, std::int32_t* screen_word);
    // Overwrites one entry (0..3, oldest first) of the depth history the next ordering reads.
    void set_depth(int slot, std::uint16_t depth) { history_.sz[static_cast<std::size_t>(slot & 3)] = depth; }
    // The depth history (oldest first): what a primitive placed without depths of its own is ordered by.
    const std::array<std::uint16_t, 4>& depth_history() const { return history_.sz; }
    // The largest entry of depth_history(): what placing a primitive feeds the frame's depth maximum with.
    std::uint32_t depth_history_max() const;
    // The whole history (depths and screen words), read and written back by drawers that project on their own.
    const ProjectionHistory& history() const { return history_; }
    void set_history(const ProjectionHistory& h) { history_ = h; }

    // ---- Matrix operations on records (each loads its first operand as the rotation first) ----
    // a.rot = a.rot * b.rot (the translation of `a` kept).
    void multiply(Transform& a, const Matrix3& b);
    // b.rot = a.rot * b.rot (the translation of `b` kept).
    void multiply_into(const Transform& a, Transform& b);
    // m.rot * v, shifted by 12: as 32-bit words, and cut to 16 bits with the saturation limit.
    Vec3i apply(const Transform& m, const Vec3s& v);
    Vec3s apply_short(const Transform& m, const Vec3s& v);
    // m.rot * v for a 32-bit vector (fixed_geometry.h apply_matrix_lv).
    Vec3i apply_long(const Transform& m, const Vec3i& v);
    // Rows of m.rot scaled by the Q12 components of `s`.
    void scale(Transform& m, const Vec3i& s);
    // m.trans = t.
    void set_matrix_translation(Transform& m, const Vec3i& t);

private:
    static constexpr std::size_t kViewStackDepth = 20;

    Transform view_{kZeroMatrix, {}};
    Matrix3 light_ = kZeroMatrix;
    Matrix3 light_color_ = kZeroMatrix;
    Vec3i background_;
    Vec3i far_color_;
    ScreenSetup screen_;
    ProjectionHistory history_;
    std::array<Transform, kViewStackDepth> stack_{};
    std::size_t stack_depth_ = 0;

};

// The game's one geometry state.
GeometryState& geometry();

}  // namespace sh::math
