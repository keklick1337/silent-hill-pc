// The glow, flashlight and ring numbers (engine/world/screen_glow.h) against values worked out by hand.

#include "engine/world/screen_glow.h"

#include "framework/sh_test.h"

namespace {
namespace w = sh::world;
}

SH_TEST(screen_glow_flashlight_fades_four_units_per_second) {
    SH_CHECK(w::flashlight_intensity_step(0, true, 256) == 1024);
    SH_CHECK(w::flashlight_intensity_step(4000, true, 256) == 4096);
    SH_CHECK(w::flashlight_intensity_step(500, false, 256) == 0);
    SH_CHECK(w::flashlight_intensity_step(4096, false, 100) == 3696);
}

SH_TEST(screen_glow_world_brightness_scales_and_clamps) {
    SH_CHECK(w::world_brightness(4096, 0) == 4);
    SH_CHECK(w::world_brightness(4096, 3, 0) == 28);
    SH_CHECK(w::world_brightness(4096, 3, 3) == 4);
    SH_CHECK(w::world_brightness(4096, 2, 3) == 0);
    SH_CHECK(w::world_brightness(4096, 7, 3) == 36);
    SH_CHECK(w::brightness_level_grey(7, 0) == 60);
    SH_CHECK(w::world_brightness(4096, 7) == 60);
    SH_CHECK(w::world_brightness(0, 3) == 0);
    SH_CHECK(w::world_brightness(1 << 20, 7) == 256);
}

SH_TEST(screen_glow_lens_flare_modes) {
    const std::array<std::int16_t, 3> rot{0, 0, 0};
    // Without the zone flag 2 the flare always follows the bone, whatever the mode.
    const w::LensFlare a = w::lens_flare_get(1000, 0, 3, rot);
    SH_CHECK(a.follows_bone && a.alpha == 1000);
    SH_CHECK(a.dir[0] == 0 && a.dir[1] == 0 && a.dir[2] == 4096);
    // Mode 0 with the flag: no alpha, fixed direction at -90 degrees yaw.
    const w::LensFlare b = w::lens_flare_get(1000, 2, 0, rot);
    SH_CHECK(!b.follows_bone && b.alpha == 0);
    SH_CHECK(b.dir[0] == -4096 && b.dir[1] == 0);
}

SH_TEST(screen_glow_ring_points_and_colours) {
    const auto p = w::screen_ring_points(160, 240);
    SH_CHECK(p[0] == (w::ScreenPoint{160, 0}) && p[1] == (w::ScreenPoint{160, 60}));
    SH_CHECK(p[2] == (w::ScreenPoint{160, 120}) && p[6] == (w::ScreenPoint{-160, 120}));
    SH_CHECK(p[10] == (w::ScreenPoint{-160, -120}) && p[14] == (w::ScreenPoint{160, -120}));
    SH_CHECK(p[16] == (w::ScreenPoint{160, 0}));
    const w::RingColors c = w::ring_colors(1, 2, 3, 30, 60, 90);
    SH_CHECK(c.fan == 0x030201u && c.steps[0] == 0 && c.steps[3] == 0x5A3C1Eu);
    SH_CHECK(c.steps[1] == 0x1E140Au && c.steps[2] == 0x3C2814u);
}

SH_TEST(screen_glow_projection_and_rings) {
    // A point in front of the half-distance plane projects straight.
    const w::Vec3 r = w::screen_point_project({100, 50, 1000}, {1, 1, 1}, 512, 160, 120);
    SH_CHECK(r.z == 1000 && r.x == 100 * 512 / 1000 + 160 && r.y == 50 * 512 / 1000 + 120);
    w::GlowRings rings;
    SH_CHECK(!w::glow_rings_build({0, 0, 0}, 4096, 0, 0, 1.0f, rings));
    SH_CHECK(w::glow_rings_build({0, 0, 8192}, 4096, 0, 0, 1.0f, rings));
    // Ring points stay inside the clamp.
    for (const auto& ring : rings.ring) {
        for (const auto& pt : ring) {
            SH_CHECK(pt.x >= -1024 && pt.x <= 1023 && pt.y >= -1024 && pt.y <= 1023);
        }
    }
}
