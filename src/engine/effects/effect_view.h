// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The view an effect drawer projects with (RENDER.md section 4): the rotation and
// translation the drawer builds its frame from, a small save stack for them, and the engine projection
// (sh::math::ViewProjection) the vertices go through. The drawers of the glow and heat fields, lens flares, fire and
// bolt pools and ground effects build these frames with the game's frame algebra on engine values, with its
// fixed-point rules (fixed_geometry.h). Under smooth_models the view also keeps the unquantised camera twin of its
// frame (engine/math/precise_capture.h), so the effects stay put against the map and the models; otherwise the
// precise records come from the integer camera.

#include <array>
#include <cstdint>
#include <vector>

#include "engine/math/fixed_geometry.h"
#include "engine/math/view_projection.h"

namespace sh::effects {

// Half the width of the screen box an effect sprite must lie in to be drawn. The drawers were written for the PS1's
// frame, 160 units to either side of the centre, and drop what lies more than `authored` (200) from it; a wider
// frame (Hor+) shows more to the sides, so the box grows by what the frame gained. Without that a flame vanishes
// before it reaches the edge of a 16:9 picture. Only drawing may go by this box: what a drawer answers, stores or
// takes from the random stream goes by the authored box, or the shape of the window would change the game.
std::int32_t screen_box_half_width(std::int32_t authored = 200);
// The host, every frame: how much wider than the PS1's half-width the visible frame is (0 at 4:3).
void set_screen_box_gain(std::int32_t gain);

class EffectView {
public:
    EffectView() = default;
    // The projection's screen and history, and the frame the game left current.
    EffectView(const math::ViewProjection& projection, const math::Transform& current);

    const math::Transform& frame() const { return frame_; }
    void set_frame(const math::Transform& t);
    void set_rotation(const math::Matrix3& rot);
    void set_translation(const math::Vec3i& trans);
    // rotation = rotation * m (the turn of a model on top of the current rotation).
    void multiply_rotation(const math::Matrix3& m);

    // The current rotation applied to a short vector, and to a long one (split into 15-bit halves).
    math::Vec3i rotate(const math::Vec3s& v) const;
    math::Vec3i rotate_long(const math::Vec3i& v) const;

    void push();
    void pop();

    // One vertex, three vertices (one call), four vertices (three, then one).
    math::PreciseVertex project(const math::Vec3s& v);
    std::array<math::PreciseVertex, 3> project3(const std::array<math::Vec3s, 3>& v);
    std::array<math::PreciseVertex, 4> project4(const std::array<math::Vec3s, 4>& v);
    // What saturated in the last projection call (for project4, in either of its two calls).
    const math::ProjectionClamps& clamps() const { return clamps_; }
    // The newest depth over 4 (the ordering value a projection call returns).
    std::int32_t order_depth() const { return projection_.depths()[3] >> 2; }
    // The four newest depths, as a polygon placed now takes them.
    const std::array<std::uint16_t, 4>& depths() const { return projection_.depths(); }

    const math::ViewProjection& projection() const { return projection_; }
    std::int16_t screen_distance() const { return projection_.screen().h; }
    void set_screen(const math::ScreenSetup& screen) { projection_.set_screen(screen); }

private:
    void sync();
    void exact_rotation();
    void exact_translation();

    math::ViewProjection projection_;
    math::Transform frame_;
    math::ExactTransform exact_;
    std::vector<math::Transform> stack_;
    std::vector<math::ExactTransform> exact_stack_;
    math::ProjectionClamps clamps_;
    bool dirty_ = true;
};

// The world-screen frame moved to a point (geometry units, Q8) of the world: the point's view position becomes the
// translation, the world-screen rotation is kept.
math::Transform frame_at(const math::Transform& world_screen, const math::Vec3s& point);
// The same for a point given as a long vector.
math::Transform frame_at_long(const math::Transform& world_screen, const math::Vec3i& point);

// The game's square root of a Q12 value (Q12 result; 0 for zero and negatives).
std::int32_t sqrt_q12(std::int32_t v);

}  // namespace sh::effects
