// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The projection an engine drawer projects its own vertices with (docs/RENDER.md section 4): a view (the
// rotation and translation of a world-screen matrix, plus the unquantised camera when the precise path has one) and
// a screen (distance, offset, depth cue), explicit values owned by the drawer.
//
// Besides the integer screen position, the depth and the precise record of each vertex (fixed_geometry.h,
// precise_projection.h), it keeps the history the original unit kept: the last four depths and the last three
// screen positions. A polygon built from a run of projections takes its per-corner depth from that history, as
// the original's packets did when they were placed, so the depth the renderer orders and normalises with is the
// original's.

#include <array>
#include <cstdint>

#include "engine/math/fixed_geometry.h"
#include "engine/math/precise_projection.h"

namespace sh::math {

// The screen half of a projection: the registers the game's view code sets once per view.
struct ScreenSetup {
    std::int16_t h = 0;    // distance to the screen
    std::int32_t ofx = 0;  // screen offset, Q16
    std::int32_t ofy = 0;
    std::int16_t dqa = 0;  // depth cue slope and base
    std::int32_t dqb = 0;
};

// How the precise record is made (the switches of the precise path).
struct PreciseSetup {
    bool precise = false;            // records are wanted at all (the precise path is on)
    PreciseOptions options;          // unquantised depth, whole-map far
    bool skip_exact_camera = false;  // debug: project from the integer camera even when the exact one is known
    bool unclamped_view = false;     // debug: precise position from the unclamped view position
    // smooth_models: view building keeps the unquantised camera (products in double, rebuilt views looked up by
    // their integers) instead of the quantised one.
    bool exact_products = false;
};

// The history of the last projections: depths SZ0..SZ3 and screen words SXY0..SXY2, oldest first.
struct ProjectionHistory {
    std::array<std::uint16_t, 4> sz{};
    std::array<std::uint32_t, 3> sxy{};  // x | y << 16
};

// What saturated in a projection call: a vertex nearer than half the screen distance (its perspective scale hit the
// limit), or a depth outside 0..0xffff. The game's drawers skip or cull primitives on these.
struct ProjectionClamps {
    bool too_near = false;
    bool depth_clamped = false;
};

class ViewProjection {
public:
    ViewProjection() = default;
    ViewProjection(const ScreenSetup& screen, const PreciseSetup& precise);

    // The view: a Q12 rotation and a translation in geometry units, and the unquantised camera of the matrix it came
    // from (null or incomplete: the precise record is made from the integer camera).
    void set_view(const Matrix3& rot, const Vec3i& trans, const ExactTransform* exact);
    void set_screen(const ScreenSetup& screen);

    // ---- View building (the geometry library's matrix calls a drawer made between projections) ----
    const Matrix3& rotation() const { return projection_.rot; }
    const Vec3i& translation() const { return projection_.trans; }
    const ExactTransform& exact() const { return exact_; }
    // The rotation times `m` on the right, the translation kept. The product's unquantised camera is taken from a
    // fresh copy of it, so it is the quantised product.
    void multiply_rotation(const Matrix3& m);
    // A translation written into a record just before it was loaded: no finer value than the integers exists.
    void set_translation(const Vec3i& trans);
    // The view as a matrix stack gives it back after foreign code ran (push, pop): the same integers, and the
    // unquantised camera is the quantised one (the stack copy had no finer record).
    void requantize();
    // A vertex through the view without the projection (rotate and translate, the 32-bit results).
    Vec3i transform(const Vec3s& v) const;
    // A 32-bit vector through the rotation only.
    Vec3i rotate_long(const Vec3i& v) const { return apply_matrix_lv(projection_.rot, v); }

    // Four vertices as the library's quad projection made them: a triple projection, then the fourth vertex; the
    // saturation of both calls combined, the depth cue of the fourth.
    std::array<PreciseVertex, 4> project4(const std::array<Vec3s, 4>& v);
    // The ordering depth the library's projection calls return: the newest depth / 4.
    std::int32_t ordering_depth() const { return history_.sz[3] >> 2; }

    // One vertex (a single projection): the result, and the history shifted by one.
    PreciseVertex project(const Vec3s& v);
    // Three vertices (a triple projection): each as project(), in order.
    std::array<PreciseVertex, 3> project3(const std::array<Vec3s, 3>& v);

    // Depth cue (0..0x1000) of the newest vertex, and what saturated in the newest projection call (any of its
    // vertices).
    std::int16_t depth_cue() const { return cue_; }
    const ProjectionClamps& clamps() const { return clamps_; }

    // The four newest depths (oldest first), as a polygon placed now takes them.
    const std::array<std::uint16_t, 4>& depths() const { return history_.sz; }
    const ProjectionHistory& history() const { return history_; }
    // Continues the history of an earlier projection (the geometry state's history when the drawer started).
    void set_history(const ProjectionHistory& history) { history_ = history; }

    // The precise records of the newest projections (up to three, oldest first), and how many there are.
    const std::array<PreciseVertex, 3>& recent() const { return recent_; }
    int recent_count() const { return recent_count_; }

    const Projection& projection() const { return projection_; }
    const ScreenSetup& screen() const { return screen_; }
    const PreciseSetup& precise() const { return precise_; }

private:
    PreciseVertex project_one(const Vec3s& v, bool cue);
    void quantized_exact(bool rotation, bool translation);
    void push(const PreciseVertex& v);

    Projection projection_;
    ExactTransform exact_;
    ScreenSetup screen_;
    PreciseSetup precise_;
    ProjectionHistory history_;
    std::int16_t cue_ = 0;
    ProjectionClamps clamps_;
    std::array<PreciseVertex, 3> recent_{};
    int recent_count_ = 0;
};

// ---- The game's view ----
// Implemented by the game's frame bridge (src/engine/effects/game/drawer_projection.cpp).

// A projection with the screen setup and precise switches the game has in force now, continuing the history of the
// projections made before.
ViewProjection game_view_projection();
// The drawer is done: the code that follows reads the history (depths, screen words, precise records) the
// projection left.
void end_game_view_projection(const ViewProjection& vp);

}  // namespace sh::math
