// The game's geometry state (src/engine/math/geometry_state.h): view and light loads, the view stack, the
// projection's 32-bit outputs and the matrix operations on records.

#include <cstdint>
#include <cstring>

#include "engine/math/geometry_state.h"
#include "engine/world/fixed_trig.h"
#include "framework/sh_test.h"

namespace {

using namespace sh::math;

constexpr std::uint32_t kGuard = 0x5A5A5A5Au;

// One 32-bit output word and the caller's neighbouring word.
struct Word {
    std::int32_t value = 0;
    std::uint32_t guard = kGuard;
};

void identity_view() {
    Transform t;
    t.trans = {};
    geometry().reset();
    geometry().load_rotation(t);
    geometry().load_translation(t);
    geometry().set_screen_offset(160, 120);
    geometry().set_screen_distance(500);
}

bool same_matrix(const Matrix3& a, const Matrix3& b) { return std::memcmp(&a, &b, sizeof a) == 0; }

}  // namespace

SH_TEST(geometry_state_projects_one_word) {
    identity_view();
    Word sxy;
    // 100 * 500 / 1000 = 50 right of the centre, 30 above it.
    const std::int32_t otz = geometry().project({100, -60, 1000}, &sxy.value);
    SH_CHECK(sxy.guard == kGuard);
    SH_CHECK((sxy.value & 0xFFFF) >= 209 && (sxy.value & 0xFFFF) <= 211);
    SH_CHECK(((sxy.value >> 16) & 0xFFFF) >= 89 && ((sxy.value >> 16) & 0xFFFF) <= 91);
    SH_CHECK(otz == 1000 / 4);
    // Far off-screen: the screen x saturates at the screen limit.
    geometry().project({30000, 0, 100}, &sxy.value);
    SH_CHECK(static_cast<std::int16_t>(sxy.value & 0xFFFF) == 0x3ff);
}

SH_TEST(geometry_state_screen_and_matrices_round_trip) {
    geometry().reset();
    SH_CHECK(geometry().screen_distance() == 1000);
    geometry().set_screen_offset(-7, 33);
    geometry().set_screen_distance(256);
    SH_CHECK(geometry().screen_offset_x() == -7 && geometry().screen_offset_y() == 33 && geometry().screen_distance() == 256);

    Transform t;
    for (int i = 0; i < 9; ++i) {
        t.rot.m[i / 3][i % 3] = static_cast<std::int16_t>(i * 1000 - 4000);
    }
    t.trans = {-5, 6, 70000};
    geometry().load_rotation(t);
    geometry().load_translation(t);
    SH_CHECK(same_matrix(geometry().rotation(), t.rot));
    const Vec3i tr = geometry().translation();
    SH_CHECK(tr.x == -5 && tr.y == 6 && tr.z == 70000);

    // Push, overwrite, pop: the pushed view is back.
    geometry().push_view();
    geometry().load_rotation(Transform{});
    geometry().set_translation({1, 2, 3});
    geometry().pop_view();
    SH_CHECK(same_matrix(geometry().rotation(), t.rot));
    SH_CHECK(geometry().translation().z == 70000);

    geometry().load_rotation_transposed(t.rot);
    const Matrix3 tt = geometry().rotation();
    SH_CHECK(tt.m[0][1] == t.rot.m[1][0] && tt.m[2][0] == t.rot.m[0][2]);

    geometry().load_light_direction({-1, 2, -3});
    const Matrix3 l = geometry().light_matrix();
    SH_CHECK(l.m[0][0] == -1 && l.m[0][1] == 2 && l.m[0][2] == -3 && l.m[1][0] == 0 && l.m[2][2] == 0);

    geometry().set_background_color(1, 2, 3);
    SH_CHECK(geometry().background().x == 16 && geometry().background().z == 48);
}

SH_TEST(geometry_state_matrix_operations) {
    Transform a;
    a.trans = {11, 22, 33};
    Matrix3 half;
    half.m[0][0] = half.m[1][1] = half.m[2][2] = 2048;
    geometry().multiply(a, half);
    SH_CHECK(a.rot.m[0][0] == 2048 && a.rot.m[1][1] == 2048 && a.trans.z == 33);

    Transform b;
    b.trans = {1, 2, 3};
    Transform scale;
    scale.rot = half;
    geometry().multiply_into(scale, b);
    SH_CHECK(b.rot.m[2][2] == 2048 && b.trans.x == 1);
    // multiply_into leaves its first operand as the loaded rotation.
    SH_CHECK(geometry().rotation().m[0][0] == 2048);

    Transform id;
    const Vec3i v = geometry().apply(id, {100, -200, 300});
    SH_CHECK(v.x == 100 && v.y == -200 && v.z == 300);
    const Vec3s vs = geometry().apply_short(scale, {100, -200, 300});
    SH_CHECK(vs.x == 50 && vs.y == -100 && vs.z == 150);
    const Vec3i lv = geometry().apply_long(id, {1 << 20, -(1 << 22), 5});
    SH_CHECK(lv.x == (1 << 20) && lv.y == -(1 << 22) && lv.z == 5);

    Transform s;
    geometry().scale(s, {4096 * 2, 2048, 4096});
    SH_CHECK(s.rot.m[0][0] == 8192 && s.rot.m[1][1] == 2048 && s.rot.m[2][2] == 4096);
    geometry().set_matrix_translation(s, {7, 8, 9});
    SH_CHECK(s.trans.y == 8);
}

SH_TEST(geometry_state_fixed_sqrt_and_atan2) {
    SH_CHECK(sh::world::fixed_sqrt(0) == 0 && sh::world::fixed_sqrt(-5) == 0);
    SH_CHECK(sh::world::fixed_sqrt(4096 * 4096) == 4096);
    SH_CHECK(sh::world::fixed_atan2(0, 0) == 0);
    SH_CHECK(sh::world::fixed_atan2(1, 0) == 1024);
    SH_CHECK(sh::world::fixed_atan2(0, -1) == 2048);
}

