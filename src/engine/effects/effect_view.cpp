// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/effects/effect_view.h"

#include <cmath>

#include "engine/math/precise_capture.h"

namespace sh::effects {

namespace {
std::int32_t g_screen_box_gain = 0;
}  // namespace

std::int32_t screen_box_half_width(std::int32_t authored) { return authored + g_screen_box_gain; }

void set_screen_box_gain(std::int32_t gain) { g_screen_box_gain = gain > 0 ? gain : 0; }

EffectView::EffectView(const math::ViewProjection& projection, const math::Transform& current)
    : projection_(projection), frame_(current) {
    exact_rotation();
    exact_translation();
}

// The unquantised value of the frame's rotation or translation: the camera's or one derived from it this frame, else
// the integers.
void EffectView::exact_rotation() {
    exact_.has_rotation = true;
    if (!projection_.precise().exact_products || !math::capture::derived_rotation(frame_.rot, exact_.rot))
        for (int i = 0; i < 9; ++i) exact_.rot[i] = frame_.rot.m[i / 3][i % 3] / 4096.0;
}

void EffectView::exact_translation() {
    exact_.has_translation = true;
    if (!projection_.precise().exact_products || !math::capture::derived_translation(frame_.trans, exact_.trans)) {
        exact_.trans[0] = frame_.trans.x;
        exact_.trans[1] = frame_.trans.y;
        exact_.trans[2] = frame_.trans.z;
    }
}

void EffectView::set_frame(const math::Transform& t) {
    frame_ = t;
    exact_rotation();
    exact_translation();
    dirty_ = true;
}

void EffectView::set_rotation(const math::Matrix3& rot) {
    frame_.rot = rot;
    exact_rotation();
    dirty_ = true;
}

void EffectView::set_translation(const math::Vec3i& trans) {
    frame_.trans = trans;
    exact_translation();
    dirty_ = true;
}

void EffectView::multiply_rotation(const math::Matrix3& m) {
    frame_.rot = math::mul_matrix(frame_.rot, m);
    double out[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = (exact_.rot[r * 3] * m.m[0][c] + exact_.rot[r * 3 + 1] * m.m[1][c] +
                              exact_.rot[r * 3 + 2] * m.m[2][c]) /
                             4096.0;
    for (int i = 0; i < 9; ++i) exact_.rot[i] = out[i];
    dirty_ = true;
}

math::Vec3i EffectView::rotate(const math::Vec3s& v) const {
    return math::matrix_vector(frame_.rot, v).full;
}

math::Vec3i EffectView::rotate_long(const math::Vec3i& v) const { return math::apply_matrix_lv(frame_.rot, v); }

void EffectView::push() {
    stack_.push_back(frame_);
    exact_stack_.push_back(exact_);
}

void EffectView::pop() {
    if (stack_.empty()) return;
    frame_ = stack_.back();
    stack_.pop_back();
    exact_ = exact_stack_.back();
    exact_stack_.pop_back();
    dirty_ = true;
}

void EffectView::sync() {
    if (!dirty_) return;
    projection_.set_view(frame_.rot, frame_.trans, projection_.precise().exact_products ? &exact_ : nullptr);
    dirty_ = false;
}

math::PreciseVertex EffectView::project(const math::Vec3s& v) {
    sync();
    const math::PreciseVertex r = projection_.project(v);
    clamps_ = projection_.clamps();
    return r;
}

std::array<math::PreciseVertex, 3> EffectView::project3(const std::array<math::Vec3s, 3>& v) {
    sync();
    const auto r = projection_.project3(v);
    clamps_ = projection_.clamps();
    return r;
}

std::array<math::PreciseVertex, 4> EffectView::project4(const std::array<math::Vec3s, 4>& v) {
    sync();
    const auto first = projection_.project3({v[0], v[1], v[2]});
    const math::ProjectionClamps first_clamps = projection_.clamps();
    const math::PreciseVertex last = projection_.project(v[3]);
    clamps_ = projection_.clamps();
    clamps_.too_near = clamps_.too_near || first_clamps.too_near;
    clamps_.depth_clamped = clamps_.depth_clamped || first_clamps.depth_clamped;
    return {first[0], first[1], first[2], last};
}

math::Transform frame_at(const math::Transform& world_screen, const math::Vec3s& point) {
    const math::Vec3i r = math::matrix_vector(world_screen.rot, point).full;
    math::Transform t;
    t.rot = world_screen.rot;
    t.trans = {r.x + world_screen.trans.x, r.y + world_screen.trans.y, r.z + world_screen.trans.z};
    const double p[3] = {static_cast<double>(point.x), static_cast<double>(point.y), static_cast<double>(point.z)};
    math::capture::register_camera_point(world_screen, p, t);
    return t;
}

math::Transform frame_at_long(const math::Transform& world_screen, const math::Vec3i& point) {
    const math::Vec3i r = math::apply_matrix_lv(world_screen.rot, point);
    math::Transform t;
    t.rot = world_screen.rot;
    t.trans = {r.x + world_screen.trans.x, r.y + world_screen.trans.y, r.z + world_screen.trans.z};
    const double p[3] = {static_cast<double>(point.x), static_cast<double>(point.y), static_cast<double>(point.z)};
    math::capture::register_camera_point(world_screen, p, t);
    return t;
}

std::int32_t sqrt_q12(std::int32_t v) {
    if (v <= 0) return 0;
    return static_cast<std::int32_t>(std::sqrt(static_cast<double>(v)) * 64.0);
}

}  // namespace sh::effects
