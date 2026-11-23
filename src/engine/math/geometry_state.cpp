// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/math/geometry_state.h"

#include <cstdio>

#include "engine/math/precise_capture.h"
#include "engine/math/precise_projection.h"
#include "render/gpu/render_state.h"


namespace sh::math {

GeometryState& geometry() {
    static GeometryState state;
    return state;
}

void GeometryState::reset() {
    screen_.h = 1000;
    screen_.dqa = -98;
    screen_.dqb = 340;
    screen_.ofx = 0;
    screen_.ofy = 0;
}

void GeometryState::set_screen_offset(std::int32_t x, std::int32_t y) {
    screen_.ofx = static_cast<std::int32_t>(static_cast<std::uint32_t>(x) << 16);
    screen_.ofy = static_cast<std::int32_t>(static_cast<std::uint32_t>(y) << 16);
}

Projection GeometryState::projection() const {
    Projection p;
    p.rot = view_.rot;
    p.trans = view_.trans;
    p.h = screen_.h;
    p.ofx = screen_.ofx;
    p.ofy = screen_.ofy;
    p.dqa = screen_.dqa;
    p.dqb = screen_.dqb;
    return p;
}

void GeometryState::load_rotation(const Matrix3& record) {
    view_.rot = record;
    sh::math::capture::rotation_loaded(&record);
}

void GeometryState::load_translation(const Transform& record) {
    view_.trans = record.trans;
    sh::math::capture::translation_loaded(&record);
}

void GeometryState::load_rotation_transposed(const Matrix3& m) {
    Matrix3 t;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            t.m[r][c] = m.m[c][r];
        }
    }
    view_.rot = t;
}

void GeometryState::load_light_direction(const Vec3s& direction) {
    light_ = kZeroMatrix;
    light_.m[0][0] = direction.x;
    light_.m[0][1] = direction.y;
    light_.m[0][2] = direction.z;
}

void GeometryState::push_view() {
    if (stack_depth_ >= kViewStackDepth) {
        std::fprintf(stderr, "geometry: view stack full (%zu entries), push ignored\n", kViewStackDepth);
        return;
    }
    stack_[stack_depth_++] = view_;
}

void GeometryState::pop_view() {
    if (stack_depth_ == 0) {
        std::fprintf(stderr, "geometry: view stack empty, pop ignored\n");
        return;
    }
    const Transform& entry = stack_[--stack_depth_];
    load_rotation(entry);
    load_translation(entry);
}

std::int32_t GeometryState::project(const Vec3s& v, std::int32_t* screen_word_out) {
    const bool precise = sh::render::precise().enabled != 0;
    PreciseOptions opt;
    opt.unquantized_depth = sh::render::precise().unquantized_depth != 0;
    opt.whole_map_far = sh::render::whole_town().reproject_far != 0;
    // The unquantised camera latched for the loaded view (debug bit 16 skips it).
    ExactTransform exact;
    if (precise && (sh::render::precise().debug & 16) == 0) {
        exact.has_rotation = capture::current_rotation(exact.rot);
        exact.has_translation = capture::current_translation(exact.trans);
    }
    const Projection p = projection();
    PreciseVertex r = project_precise(p, v, nullptr, &exact, opt);
    if (precise && (sh::render::precise().debug & 32) != 0) {
        // Debug: the precise position from the unclamped view position.
        const auto& m = p.rot.m;
        const std::int64_t depth =
            ((static_cast<std::int64_t>(p.trans.z) << 12) + m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z) >> 12;
        if (depth > 0) {
            const double ratio = static_cast<double>(p.h) / static_cast<double>(depth);
            r.sx = static_cast<float>(static_cast<double>(p.ofx) / 65536.0 + static_cast<double>(r.v.view.x) * ratio);
            r.sy = static_cast<float>(static_cast<double>(p.ofy) / 65536.0 + static_cast<double>(r.v.view.y) * ratio);
            r.w = static_cast<float>(depth);
        }
    }
    if (opt.whole_map_far) sh::render::whole_town().last_depth = r.unclamped_depth;
    if (precise) {
        sh::render::precise().screen_offset_x = r.ofx;
        sh::render::precise().screen_offset_y = r.ofy;
        sh::render::precise().projection_distance = r.h;
    }

    history_.sz[0] = history_.sz[1];
    history_.sz[1] = history_.sz[2];
    history_.sz[2] = history_.sz[3];
    history_.sz[3] = r.v.sz;
    history_.sxy[0] = history_.sxy[1];
    history_.sxy[1] = history_.sxy[2];
    history_.sxy[2] = screen_word(r.v);
    *screen_word_out = static_cast<std::int32_t>(history_.sxy[2]);
    return history_.sz[3] >> 2;
}

std::uint32_t GeometryState::depth_history_max() const {
    std::uint32_t mx = 0;
    for (const std::uint16_t z : history_.sz) mx = z > mx ? z : mx;
    return mx;
}

void GeometryState::multiply(Transform& a, const Matrix3& b) {
    load_rotation(a);
    // The product goes through a separate record, as the library's did: the precise twin of `a` is copied from it.
    Transform product;
    product.rot = mul_matrix(a.rot, b);
    a.rot = product.rot;
    if (sh::render::precise().enabled) {
        sh::math::capture::copy_rotation(&a, &product);
    }
}

void GeometryState::multiply_into(const Transform& a, Transform& b) {
    load_rotation(a);
    Transform product;
    product.rot = mul_matrix(a.rot, b.rot);
    b.rot = product.rot;
    if (sh::render::precise().enabled) {
        sh::math::capture::copy_rotation(&b, &product);
    }
}

Vec3i GeometryState::apply(const Transform& m, const Vec3s& v) {
    load_rotation(m);
    return matrix_vector(m.rot, v).full;
}

Vec3s GeometryState::apply_short(const Transform& m, const Vec3s& v) {
    load_rotation(m);
    return matrix_vector(m.rot, v).saturated;
}

Vec3i GeometryState::apply_long(const Transform& m, const Vec3i& v) {
    load_rotation(m);
    return apply_matrix_lv(m.rot, v);
}

void GeometryState::scale(Transform& m, const Vec3i& s) {
    double exact[9];
    const bool have_exact = sh::render::precise().enabled && sh::math::capture::rotation_of(&m, exact);
    if (have_exact) {
        const double factor[3] = {s.x / 4096.0, s.y / 4096.0, s.z / 4096.0};
        for (int i = 0; i < 9; ++i) {
            exact[i] *= factor[i / 3];
        }
    }
    m.rot = scale_matrix(m.rot, s);
    if (sh::render::precise().enabled) {
        if (have_exact) {
            sh::math::capture::note_rotation(&m, exact);
        } else {
            sh::math::capture::forget_rotation(&m);
        }
    }
}

void GeometryState::set_matrix_translation(Transform& m, const Vec3i& t) {
    m.trans = t;
    if (sh::render::precise().enabled) {
        sh::math::capture::forget_translation(&m);
    }
}

}  // namespace sh::math
