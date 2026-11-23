// The precise projection record (engine/math/precise_projection.h) on hand-worked values.

#include "engine/math/precise_projection.h"

#include "framework/sh_test.h"

namespace {
namespace g = sh::math;

g::Projection screen_500() {
    g::Projection p;
    p.h = 500;
    p.ofx = 160 << 16;
    p.ofy = 120 << 16;
    return p;
}
}  // namespace

SH_TEST(precise_projection_matches_integer_result_and_gives_float_record) {
    const g::Projection p = screen_500();
    g::PreciseOptions opt;
    const g::PreciseVertex r = g::project_precise(p, {100, -60, 1000}, nullptr, nullptr, opt);
    const g::Projected plain = g::project_vertex(p, {100, -60, 1000});
    SH_CHECK(r.v.sx == plain.sx && r.v.sy == plain.sy && r.v.sz == plain.sz);
    SH_CHECK(r.sx == 160.0f + 50.0f && r.sy == 120.0f - 30.0f);  // 100 * 500 / 1000 and -60 * 500 / 1000
    SH_CHECK(r.w == 1000.0f);
    SH_CHECK(r.view_x == 100.0f && r.view_y == -60.0f && r.view_z == 1000.0f);
    SH_CHECK(r.ofx == 160.0f && r.ofy == 120.0f && r.h == 500.0f);
}

SH_TEST(precise_projection_behind_the_screen_is_affine) {
    const g::Projection p = screen_500();
    const g::PreciseVertex r = g::project_precise(p, {10, 10, -5}, nullptr, nullptr, {});
    SH_CHECK(r.w == 0.0f);
    SH_CHECK(r.sx == static_cast<float>(r.v.sx) && r.sy == static_cast<float>(r.v.sy));
}

SH_TEST(precise_projection_uses_the_exact_camera_only_with_both_parts) {
    const g::Projection p = screen_500();
    g::ExactTransform cam;
    cam.trans[2] = 1000.5;  // half a unit deeper than the integer translation (zero) plus the vertex
    cam.has_rotation = true;
    g::PreciseVertex r = g::project_precise(p, {100, 0, 0}, nullptr, &cam, {});
    SH_CHECK(r.w == 0.0f);  // integer depth 0: nothing to divide by, and the exact camera is not complete yet
    cam.has_translation = true;
    r = g::project_precise(p, {100, 0, 0}, nullptr, &cam, {});
    SH_CHECK(r.w == 1000.5f);
    SH_CHECK(r.sx == static_cast<float>(160.0 + 100.0 * 500.0 / 1000.5));
    // An exact vertex replaces the integer one in the exact path only.
    const g::Vec3d exact_vertex{100.25, 0, 0};
    r = g::project_precise(p, {100, 0, 0}, &exact_vertex, &cam, {});
    SH_CHECK(r.sx == static_cast<float>(160.0 + 100.25 * 500.0 / 1000.5));
}

SH_TEST(precise_projection_far_mode_keeps_the_unclamped_depth) {
    g::Projection p = screen_500();
    g::PreciseOptions opt;
    opt.whole_map_far = true;
    const g::PreciseVertex r = g::project_precise(p, {0, 0, 30000}, nullptr, nullptr, opt);
    SH_CHECK(r.v.sz == 30000 && r.w == 30000.0f && r.unclamped_depth == 30000);
    p.trans.z = 70000;  // beyond the 16-bit depth register
    const g::PreciseVertex far = g::project_precise(p, {1000, 0, 0}, nullptr, nullptr, opt);
    SH_CHECK(far.v.sz == 0xffff && far.w == 70000.0f && far.unclamped_depth == 70000);
}
