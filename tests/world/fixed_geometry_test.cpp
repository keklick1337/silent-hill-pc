// The fixed-point projection, matrix and colour blend math (engine/math/fixed_geometry.h) against values worked out by
// hand.

#include "engine/math/fixed_geometry.h"

#include "framework/sh_test.h"

namespace {
namespace g = sh::math;
}

SH_TEST(fixed_geometry_divide_is_a_reciprocal_with_overflow_marker) {
    SH_CHECK(g::divide(1000, 2000) >= 0x7ff0 && g::divide(1000, 2000) <= 0x8010);  // 0.5 in 16.16
    SH_CHECK(g::divide(2000, 1000) == 0xffffffffu);  // numerator at least twice the denominator
    SH_CHECK(g::divide(1, 0) == 0xffffffffu);
}

SH_TEST(fixed_geometry_projects_a_vertex_in_front_of_the_screen) {
    g::Projection p;
    p.h = 500;
    p.ofx = 160 << 16;
    p.ofy = 120 << 16;
    // The identity matrix: view space equals the vertex; x = 100 at depth 1000 lands at 100 * 500 / 1000 = 50.
    const g::Projected r = g::project_vertex(p, {100, -60, 1000});
    SH_CHECK(r.view.x == 100 && r.view.y == -60 && r.view.z == 1000);
    SH_CHECK(r.sz == 1000);
    SH_CHECK(r.sx > 160 + 49 - 1 && r.sx < 160 + 50 + 1);
    SH_CHECK(!r.too_near && !r.depth_clamped);
}

SH_TEST(fixed_geometry_projection_saturates_depth_and_screen) {
    g::Projection p;
    p.h = 500;
    // Behind the eye: depth 0, clamped, and the perspective scale saturates (too near).
    const g::Projected r = g::project_vertex(p, {0, 0, -5});
    SH_CHECK(r.sz == 0 && r.depth_clamped && r.too_near && r.h_over_sz == 0x1ffff);
    // Nearer than half the screen distance: the scale saturates without a depth clamp.
    const g::Projected near = g::project_vertex(p, {0, 0, 200});
    SH_CHECK(near.too_near && !near.depth_clamped && near.h_over_sz == 0x1ffff);
    const g::Projected far = g::project_vertex(p, {30000, 0, 100});
    SH_CHECK(far.sx == 0x3ff);
    // A view position beyond 16 bits keeps its 32-bit value and saturates the short one.
    p.trans = {100000, 0, 1000};
    const g::Projected wide = g::project_vertex(p, {0, 0, 0});
    SH_CHECK(wide.view.x == 100000 && wide.view_short.x == 0x7fff);
}

SH_TEST(fixed_geometry_matrix_vector_adds_the_offset) {
    g::Matrix3 m;
    const g::MatVec r = g::matrix_vector(m, {10, 20, 30}, g::Offset::Translation, {1, 2, 3});
    SH_CHECK(r.full.x == 11 && r.full.y == 22 && r.full.z == 33);
    const g::MatVec big = g::matrix_vector(m, {32767, 0, 0}, g::Offset::Translation, {100000, 0, 0});
    SH_CHECK(big.full.x == 132767 && big.saturated.x == 0x7fff);
    const g::MatVec neg = g::matrix_vector(m, {-5, 7, 0}, g::Offset::None, {}, true);
    SH_CHECK(neg.full.x == -5 && neg.saturated.x == 0 && neg.saturated.y == 7);
}

SH_TEST(fixed_geometry_depth_cue_blends_towards_the_far_colour) {
    // cue = 0: the colour itself (the 8-bit value stays); cue = 1.0: the far colour.
    const g::ColorOut none = g::dpcs({100, 50, 200}, 0, {0, 0, 0});
    SH_CHECK(none.rgb.r == 100 && none.rgb.g == 50 && none.rgb.b == 200);
    const g::ColorOut full = g::dpcs({100, 50, 200}, 0x1000, {0, 0, 0});
    SH_CHECK(full.rgb.r == 0 && full.rgb.g == 0 && full.rgb.b == 0);
    const g::ColorOut half = g::dpcs({100, 50, 200}, 0x800, {0, 0, 0});
    SH_CHECK(half.rgb.r == 50 && half.rgb.g == 25 && half.rgb.b == 100);
    // An intensity of 1.0 is the plain colour; halfway to a far colour of 16 * 200.
    const g::ColorOut lit = g::dpcl({100, 50, 200}, {4096, 4096, 4096}, 0x800, {3200, 3200, 3200});
    SH_CHECK(lit.rgb.r == 150 && lit.rgb.g == 125 && lit.rgb.b == 200);
    // The depth cue line: base + slope * scale, in Q12, saturated to 0..0x1000.
    g::Projection p;
    p.dqa = -100;
    p.dqb = 0x1400000;
    SH_CHECK(g::depth_cue(p, 0) == 0x1000);
    SH_CHECK(g::depth_cue(p, 0x10000) == ((0x1400000 - 100 * 0x10000) >> 12));
}

SH_TEST(fixed_geometry_scaled_and_lit_colours) {
    const g::ColorOut half = g::gpf({4096, 2048, -100}, 2048);
    SH_CHECK(half.full.x == 2048 && half.full.y == 1024 && half.full.z == -50);
    SH_CHECK(half.rgb.r == 128 && half.rgb.g == 64 && half.rgb.b == 0);
    // One light along x, a white colour matrix, a background of 16: the lit value of the x normal plus 16.
    g::Matrix3 light{{{4096, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
    g::Matrix3 color{{{4096, 0, 0}, {4096, 0, 0}, {4096, 0, 0}}};
    const g::ColorOut c = g::normal_color(light, color, {16, 16, 16}, {2048, 0, 0}, true);
    SH_CHECK(c.full.x == 2064 && c.full.y == 2064 && c.rgb.r == 129);
    const g::ColorOut back = g::normal_color(light, color, {0, 0, 0}, {-2048, 0, 0}, true);
    SH_CHECK(back.full.x == 0 && back.saturated.x == 0);
}

SH_TEST(fixed_geometry_rotation_builders_start_from_identity) {
    const g::Matrix3 a = g::rot_matrix_zxy_neg({0, 0, 0});
    SH_CHECK(a.m[0][0] == 4096 && a.m[1][1] == 4096 && a.m[2][2] == 4096 && a.m[0][1] == 0 && a.m[2][0] == 0);
    // A quarter turn about Y sends the x axis to -z in the camera convention.
    const g::Matrix3 q = g::rot_matrix_zxy_neg({0, 1024, 0});
    SH_CHECK(q.m[0][2] == 4096 && q.m[2][0] == -4096);
    const g::Matrix3 z = g::rot_matrix_z(1024);
    SH_CHECK(z.m[0][1] == -4096 && z.m[1][0] == 4096 && z.m[2][2] == 4096);
}

SH_TEST(fixed_geometry_vector_helpers) {
    std::int64_t sq = 0;
    const g::Vec3i u = g::vector_normal({0, 8192, 0}, &sq);
    SH_CHECK(u.x == 0 && u.y == 4096 && u.z == 0 && sq == 8192LL * 8192);
    const g::Vec3i zero = g::vector_normal({0, 0, 0});
    SH_CHECK(zero.x == 0 && zero.y == 0 && zero.z == 0);
    const g::Vec3i c = g::outer_product12({4096, 0, 0}, {0, 4096, 0});
    SH_CHECK(c.x == 0 && c.y == 0 && c.z == 4096);
}

SH_TEST(fixed_geometry_matrix_product_with_identity_keeps_the_matrix) {
    g::Matrix3 a;
    a.m[0][1] = 700;
    a.m[2][0] = -1234;
    const g::Matrix3 r = g::mul_matrix(g::Matrix3{}, a);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            SH_CHECK(r.m[i][j] == a.m[i][j]);
        }
    }
    // Elements of a product saturate at 16 bits.
    g::Matrix3 big{{{30000, 30000, 0}, {0, 4096, 0}, {0, 0, 4096}}};
    g::Matrix3 ones{{{4096, 0, 0}, {4096, 4096, 0}, {0, 0, 4096}}};
    SH_CHECK(g::mul_matrix(big, ones).m[0][0] == 0x7fff);
}

SH_TEST(fixed_geometry_composition_adds_the_outer_translation) {
    g::Transform outer, inner;
    outer.trans = {10, 20, 30};
    inner.trans = {100, -200, 300};
    const g::Transform c = g::comp_matrix(outer, inner);
    SH_CHECK(c.trans.x == 110 && c.trans.y == -180 && c.trans.z == 330);
}

SH_TEST(fixed_geometry_wide_vector_goes_through_a_rotation_unchanged_for_identity) {
    const g::Vec3i v{123456, -654321, 40000};
    const g::Vec3i r = g::apply_matrix_lv(g::Matrix3{}, v);
    SH_CHECK(r.x == v.x && r.y == v.y && r.z == v.z);
}

SH_TEST(fixed_geometry_scale_matrix_scales_rows) {
    g::Matrix3 m;
    const g::Matrix3 s = g::scale_matrix(m, {2048, 4096, -4096});
    SH_CHECK(s.m[0][0] == 2048 && s.m[1][1] == 4096 && s.m[2][2] == -4096 && s.m[0][1] == 0);
}

SH_TEST(fixed_geometry_far_projection_keeps_the_true_depth) {
    g::Projection p;
    p.h = 500;
    p.trans = {0, 0, 200000};
    const g::ProjectedFar r = g::project_vertex_far(p, {0, 0, 0});
    SH_CHECK(r.v.sz == 0xffff && r.unclamped_depth == 200000);
}
