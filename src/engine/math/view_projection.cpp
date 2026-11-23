// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/math/view_projection.h"

#include "engine/math/precise_capture.h"

namespace sh::math {

ViewProjection::ViewProjection(const ScreenSetup& screen, const PreciseSetup& precise) : precise_(precise) {
    set_screen(screen);
}

void ViewProjection::set_view(const Matrix3& rot, const Vec3i& trans, const ExactTransform* exact) {
    projection_.rot = rot;
    projection_.trans = trans;
    exact_ = exact != nullptr ? *exact : ExactTransform{};
}

void ViewProjection::quantized_exact(bool rotation, bool translation) {
    // Without the precise path nothing is latched at all.
    if (rotation) {
        exact_.has_rotation = precise_.precise;
        if (!precise_.exact_products || !capture::derived_rotation(projection_.rot, exact_.rot))
            for (int i = 0; i < 9; ++i) exact_.rot[i] = static_cast<double>(projection_.rot.m[i / 3][i % 3]) / 4096.0;
    }
    if (translation) {
        exact_.has_translation = precise_.precise;
        if (precise_.exact_products && capture::derived_translation(projection_.trans, exact_.trans)) return;
        exact_.trans[0] = static_cast<double>(projection_.trans.x);
        exact_.trans[1] = static_cast<double>(projection_.trans.y);
        exact_.trans[2] = static_cast<double>(projection_.trans.z);
    }
}

void ViewProjection::multiply_rotation(const Matrix3& m) {
    projection_.rot = mul_matrix(projection_.rot, m);
    if (precise_.exact_products && exact_.has_rotation) {
        double out[9];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                out[r * 3 + c] = (exact_.rot[r * 3] * m.m[0][c] + exact_.rot[r * 3 + 1] * m.m[1][c] +
                                  exact_.rot[r * 3 + 2] * m.m[2][c]) /
                                 4096.0;
        for (int i = 0; i < 9; ++i) exact_.rot[i] = out[i];
        return;
    }
    quantized_exact(true, false);
}

void ViewProjection::set_translation(const Vec3i& trans) {
    projection_.trans = trans;
    quantized_exact(false, true);
}

void ViewProjection::requantize() { quantized_exact(true, true); }

Vec3i ViewProjection::transform(const Vec3s& v) const {
    return matrix_vector(projection_.rot, v, Offset::Translation, projection_.trans).full;
}

std::array<PreciseVertex, 4> ViewProjection::project4(const std::array<Vec3s, 4>& v) {
    const std::array<PreciseVertex, 3> first = project3({v[0], v[1], v[2]});
    const ProjectionClamps first_clamps = clamps_;
    std::array<PreciseVertex, 4> out{first[0], first[1], first[2], project(v[3])};
    clamps_.too_near |= first_clamps.too_near;
    clamps_.depth_clamped |= first_clamps.depth_clamped;
    return out;
}

void ViewProjection::set_screen(const ScreenSetup& screen) {
    screen_ = screen;
    projection_.h = screen.h;
    projection_.ofx = screen.ofx;
    projection_.ofy = screen.ofy;
    projection_.dqa = screen.dqa;
    projection_.dqb = screen.dqb;
}

void ViewProjection::push(const PreciseVertex& v) {
    history_.sz[0] = history_.sz[1];
    history_.sz[1] = history_.sz[2];
    history_.sz[2] = history_.sz[3];
    history_.sz[3] = v.v.sz;
    history_.sxy[0] = history_.sxy[1];
    history_.sxy[1] = history_.sxy[2];
    history_.sxy[2] = screen_word(v.v);
    recent_[0] = recent_[1];
    recent_[1] = recent_[2];
    recent_[2] = v;
    if (recent_count_ < 3) ++recent_count_;
}

PreciseVertex ViewProjection::project(const Vec3s& v) {
    clamps_ = {};
    return project_one(v, true);
}

PreciseVertex ViewProjection::project_one(const Vec3s& v, bool cue) {
    PreciseVertex out = project_precise(projection_, v, nullptr, precise_.skip_exact_camera ? nullptr : &exact_,
                                        precise_.options);
    clamps_.too_near = clamps_.too_near || out.v.too_near;
    clamps_.depth_clamped = clamps_.depth_clamped || out.v.depth_clamped;
    if (precise_.unclamped_view) {
        const auto& r = projection_.rot.m;
        const std::int64_t depth =
            ((static_cast<std::int64_t>(projection_.trans.z) << 12) + r[2][0] * v.x + r[2][1] * v.y + r[2][2] * v.z) >>
            12;
        if (depth > 0) {
            const double ratio = static_cast<double>(projection_.h) / static_cast<double>(depth);
            out.sx = static_cast<float>(static_cast<double>(projection_.ofx) / 65536.0 +
                                        static_cast<double>(out.v.view.x) * ratio);
            out.sy = static_cast<float>(static_cast<double>(projection_.ofy) / 65536.0 +
                                        static_cast<double>(out.v.view.y) * ratio);
            out.w = static_cast<float>(depth);
        }
    }
    if (cue) cue_ = sh::math::depth_cue(projection_, out.v.h_over_sz);
    push(out);
    return out;
}

std::array<PreciseVertex, 3> ViewProjection::project3(const std::array<Vec3s, 3>& v) {
    // One saturation record for the three vertices; the depth cue is computed for the last one only.
    clamps_ = {};
    std::array<PreciseVertex, 3> out;
    for (std::size_t k = 0; k < 3; ++k) out[k] = project_one(v[k], k == 2);
    return out;
}

}  // namespace sh::math
