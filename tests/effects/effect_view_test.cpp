// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the effect drawers' view (sh::effects::EffectView): the frame algebra against the fixed-point
// functions it is made of, the projection against the engine projection, the save stack and the helpers.

#include "engine/effects/effect_view.h"

#include "framework/sh_test.h"

using namespace sh;
using namespace sh::effects;

namespace {

math::ViewProjection screen() {
    math::ScreenSetup s;
    s.h = 320;
    s.ofx = 160 << 16;
    s.ofy = 112 << 16;
    s.dqa = -100;
    s.dqb = 0x1400000;
    return math::ViewProjection(s, math::PreciseSetup{});
}

math::Transform camera() {
    math::Transform t;
    t.rot = math::rot_matrix_zxy_neg({120, -700, 30});
    t.trans = {300, -200, 4000};
    return t;
}

bool same(const math::Matrix3& a, const math::Matrix3& b) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            if (a.m[r][c] != b.m[r][c]) return false;
        }
    }
    return true;
}

}  // namespace

SH_TEST(effect_view_projects_like_the_engine_projection) {
    EffectView view(screen(), camera());
    math::ViewProjection ref = screen();
    ref.set_view(camera().rot, camera().trans, nullptr);
    const math::Vec3s v{100, -50, 25};
    const math::PreciseVertex a = view.project(v);
    const math::PreciseVertex b = ref.project(v);
    SH_CHECK(a.v.sx == b.v.sx && a.v.sy == b.v.sy && a.v.sz == b.v.sz);
    SH_CHECK(view.clamps().too_near == ref.clamps().too_near &&
             view.clamps().depth_clamped == ref.clamps().depth_clamped);
    SH_CHECK(view.order_depth() == (b.v.sz >> 2));
    SH_CHECK(view.depths() == ref.depths());
}

SH_TEST(effect_view_project4_is_a_triple_then_one_with_both_clamps) {
    EffectView view(screen(), camera());
    math::ViewProjection ref = screen();
    ref.set_view(camera().rot, camera().trans, nullptr);
    // The last corner behind the eye saturates the projection as too near, the first three do not.
    const std::array<math::Vec3s, 4> c{math::Vec3s{0, 0, 0}, math::Vec3s{50, 0, 0}, math::Vec3s{0, 50, 0},
                                      math::Vec3s{0, 0, -20000}};
    const auto r = view.project4(c);
    const auto t = ref.project3({c[0], c[1], c[2]});
    const bool near3 = ref.clamps().too_near;
    const math::PreciseVertex l = ref.project(c[3]);
    SH_CHECK(r[0].v.sx == t[0].v.sx && r[2].v.sy == t[2].v.sy && r[3].v.sx == l.v.sx);
    SH_CHECK(!near3 && ref.clamps().too_near && view.clamps().too_near);
    SH_CHECK(view.depths() == ref.depths());
}

SH_TEST(effect_view_frame_algebra) {
    EffectView view(screen(), camera());
    const math::Matrix3 turn = math::rot_matrix_xyz({0, 512, 0});
    view.multiply_rotation(turn);
    SH_CHECK(same(view.frame().rot, math::mul_matrix(camera().rot, turn)));

    view.push();
    view.set_rotation(math::Matrix3{});
    view.set_translation({1, 2, 3});
    SH_CHECK(view.frame().trans.z == 3);
    view.pop();
    SH_CHECK(same(view.frame().rot, math::mul_matrix(camera().rot, turn)));
    SH_CHECK(view.frame().trans.x == 300);

    // A frame moved to a point: the rotated point plus the translation, the rotation kept.
    const math::Transform at = frame_at(camera(), {256, 0, -128});
    const math::Vec3i r =
        math::matrix_vector(camera().rot, {256, 0, -128}).full;
    SH_CHECK(at.trans.x == r.x + 300 && at.trans.y == r.y - 200 && at.trans.z == r.z + 4000);
    SH_CHECK(same(at.rot, camera().rot));
    const math::Transform at_long = frame_at_long(camera(), {256, 0, -128});
    SH_CHECK(at_long.trans.x == at.trans.x && at_long.trans.z == at.trans.z);
}

SH_TEST(effect_view_square_root) {
    SH_CHECK(sqrt_q12(0) == 0);
    SH_CHECK(sqrt_q12(-5) == 0);
    SH_CHECK(sqrt_q12(4096) == 4096);
    SH_CHECK(sqrt_q12(4096 * 4) == 8192);
    SH_CHECK(sqrt_q12(2048) == 2896);
}
