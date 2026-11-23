// The engine's view projection (engine/math/view_projection.h): results equal the functional projection, the
// history shifts like the original's depth and screen history, the triple projection's saturation and depth cue.

#include "engine/math/view_projection.h"

#include "framework/sh_test.h"

namespace {
namespace g = sh::math;

g::ViewProjection make() {
    g::ScreenSetup s;
    s.h = 320;
    s.ofx = 160 << 16;
    s.ofy = 112 << 16;
    s.dqa = -100;
    s.dqb = 0x1400000;
    g::ViewProjection vp(s, {});
    vp.set_view(g::Matrix3{}, {0, 0, 0}, nullptr);
    return vp;
}
}  // namespace

SH_TEST(view_projection_vertex_equals_functional_projection) {
    g::ViewProjection vp = make();
    const g::Vec3s v{100, -60, 1000};
    const g::PreciseVertex r = vp.project(v);
    const g::Projected plain = g::project_vertex(vp.projection(), v);
    const std::int16_t cue = g::depth_cue(vp.projection(), plain.h_over_sz);
    SH_CHECK(r.v.sx == plain.sx && r.v.sy == plain.sy && r.v.sz == plain.sz);
    SH_CHECK(!vp.clamps().too_near && !vp.clamps().depth_clamped && vp.depth_cue() == cue);
}

SH_TEST(view_projection_history_shifts_like_the_original) {
    g::ViewProjection vp = make();
    g::ProjectionHistory h;
    h.sz = {1, 2, 3, 4};
    h.sxy = {10, 20, 30};
    vp.set_history(h);
    const g::PreciseVertex a = vp.project({0, 0, 500});
    SH_CHECK(vp.depths()[0] == 2 && vp.depths()[1] == 3 && vp.depths()[2] == 4 && vp.depths()[3] == a.v.sz);
    SH_CHECK(vp.history().sxy[0] == 20 && vp.history().sxy[1] == 30 && vp.history().sxy[2] == g::screen_word(a.v));
    const auto t = vp.project3({g::Vec3s{0, 0, 600}, g::Vec3s{0, 0, 700}, g::Vec3s{0, 0, 800}});
    SH_CHECK(vp.depths()[0] == a.v.sz && vp.depths()[1] == t[0].v.sz && vp.depths()[2] == t[1].v.sz &&
             vp.depths()[3] == t[2].v.sz);
    SH_CHECK(t[0].v.sz == 600 && t[2].v.sz == 800);
}

SH_TEST(view_projection_triple_cue_is_the_last_vertex) {
    g::ViewProjection vp = make();
    const auto t = vp.project3({g::Vec3s{0, 0, 100}, g::Vec3s{0, 0, 200}, g::Vec3s{0, 0, 4000}});
    SH_CHECK(vp.depth_cue() == g::depth_cue(vp.projection(), t[2].v.h_over_sz));
}

SH_TEST(view_projection_exact_camera_feeds_the_precise_record) {
    g::ScreenSetup s;
    s.h = 256;
    g::PreciseSetup p;
    p.precise = true;
    g::ViewProjection vp(s, p);
    g::ExactTransform exact;
    exact.has_rotation = exact.has_translation = true;
    exact.trans[2] = 1000.5;
    vp.set_view(g::Matrix3{}, {0, 0, 1000}, &exact);
    const g::PreciseVertex r = vp.project({0, 64, 0});
    SH_CHECK(r.v.sz == 1000);
    SH_CHECK(r.sy == static_cast<float>(64.0 * 256.0 / 1000.5));
    vp.set_view(g::Matrix3{}, {0, 0, 1000}, nullptr);
    const g::PreciseVertex q = vp.project({0, 64, 0});
    SH_CHECK(q.sy == static_cast<float>(64.0 * 256.0 / 1000.0));
}

SH_TEST(view_projection_multiply_rotation_quantises_the_exact_camera) {
    g::ScreenSetup s;
    s.h = 256;
    g::PreciseSetup p;
    p.precise = true;
    g::ViewProjection vp(s, p);
    g::ExactTransform exact;
    exact.has_rotation = exact.has_translation = true;
    exact.rot[0] = 1.0001;
    exact.trans[2] = 1000.5;
    vp.set_view(g::Matrix3{}, {0, 0, 1000}, &exact);
    g::Matrix3 half;
    half.m[0][0] = 2048;
    vp.multiply_rotation(half);
    SH_CHECK(vp.rotation().m[0][0] == 2048 && vp.rotation().m[1][1] == 4096);
    SH_CHECK(vp.exact().has_rotation && vp.exact().rot[0] == 0.5 && vp.exact().rot[4] == 1.0);
    SH_CHECK(vp.exact().trans[2] == 1000.5);
    vp.set_translation({1, 2, 3});
    SH_CHECK(vp.translation().z == 3 && vp.exact().has_translation && vp.exact().trans[2] == 3.0);
    vp.set_view(g::Matrix3{}, {0, 0, 1000}, &exact);
    vp.requantize();
    SH_CHECK(vp.exact().rot[0] == 1.0 && vp.exact().trans[2] == 1000.0);

    g::ViewProjection off(s, {});
    off.set_view(g::Matrix3{}, {0, 0, 1000}, nullptr);
    off.multiply_rotation(half);
    SH_CHECK(!off.exact().has_rotation);
}

SH_TEST(view_projection_transform_and_rotate_long) {
    g::ViewProjection vp = make();
    g::Matrix3 r;
    r.m[0][0] = 0;
    r.m[0][1] = -4096;
    r.m[1][0] = 4096;
    r.m[1][1] = 0;
    vp.set_view(r, {10, 20, 30}, nullptr);
    const g::Vec3i t = vp.transform({100, 200, 300});
    SH_CHECK(t.x == -190 && t.y == 120 && t.z == 330);
    const g::Vec3i l = vp.rotate_long({100000, 0, 5});
    SH_CHECK(l.x == 0 && l.y == 100000 && l.z == 5);
}

SH_TEST(view_projection_quad_is_a_triple_then_a_single) {
    g::ViewProjection vp = make();
    const auto q = vp.project4({g::Vec3s{0, 0, 400}, g::Vec3s{0, 0, 500}, g::Vec3s{0, 0, 600}, g::Vec3s{0, 0, 800}});
    SH_CHECK(vp.depths()[0] == 400 && vp.depths()[3] == 800 && q[3].v.sz == 800);
    SH_CHECK(vp.ordering_depth() == 200);
}
