// The world environment numbers (engine/world/world_env.h) against values worked out by hand from the original's
// behaviour: fog ramp and lookups, light falloff, darkness, light set-up and the config file.

#include "engine/world/world_env.h"

#include "framework/sh_test.h"

namespace {
namespace w = sh::world;
}

SH_TEST(world_env_fog_ramp_follows_curve) {
    const w::WorldEnvParams p;
    // Near 13.5 m = 3456 in Q8: depth range is 2^12, 32 steps of 128 per 4096.
    const w::FogRamp f = w::fog_ramp_build(p, 3456, 3712);
    SH_CHECK(f.depth_shift == 12);
    SH_CHECK(f.near_distance == 3456 && f.far_distance == 3712);
    SH_CHECK(f.ramp[0] == 0);
    SH_CHECK(f.ramp[127] == 0xFF || f.ramp[127] >= f.ramp[100]);  // non-decreasing towards the end
    for (int i = 1; i < w::kFogRampSize; ++i) {
        SH_CHECK(f.ramp[i] >= f.ramp[i - 1]);
    }
    // Past the end of the depth range the fog is complete.
    SH_CHECK(w::fog_ramp_u8(f, 1 << 20) == 255);
    SH_CHECK(w::fog_ramp_q12(f, 1 << 20) == 4096);
    SH_CHECK(w::fog_ramp_q12(f, -16) == 0);
    SH_CHECK(w::fog_ramp_u8(f, 0) == f.ramp[0]);
}

SH_TEST(world_env_fog_ramp_guards_zero_distance) {
    const w::FogRamp f = w::fog_ramp_build(w::WorldEnvParams{}, 0, 100);
    SH_CHECK(f.near_distance == 1);
    SH_CHECK(f.depth_shift == 0);
    SH_CHECK(f.ramp[127] >= 250 && f.ramp[127] >= f.ramp[126]);  // the curve runs to its last bin
}

SH_TEST(world_env_light_falloff_and_darkness) {
    const w::WorldEnvParams p;
    const auto t = w::light_falloff_compute(p, 4096, {0, 0, 0});
    // Zero direction: both sides weigh (5324 * 4096) / 9420 = 2314.
    SH_CHECK(t[0][0].x == 0 && t[0][0].base == 204 && t[0][0].slope != 0);
    SH_CHECK(t[1][0].base == t[1][1].base && t[1][0].slope == t[1][1].slope);
    // Light off: only the ambient shows, capped.
    SH_CHECK(w::darkness_at(t, false, 32 * 10, 255, {0, 0, 0}) == 10);
    SH_CHECK(w::darkness_at(t, false, 32 * 300, 200, {0, 0, 0}) == 200);
    SH_CHECK(w::darkness_at(t, false, -32 * 5, 200, {0, 0, 0}) == 0);
    // Light on at the light's own position: the lowest axis value, at most 255.
    SH_CHECK(w::darkness_at(t, true, 0, 255, {0, 0, 0}) == 204);
}

SH_TEST(world_env_light_setup_scales_by_overrides) {
    const w::ColorScale off;
    const w::LightSetup a = w::light_setup(1, 4096, 200, {3200, 640, 64}, 7, off, off);
    SH_CHECK(a.mode == 1 && a.cap == 200 && a.brightness == 7);
    SH_CHECK(a.world_tint[0] == 100 && a.world_tint[1] == 20 && a.world_tint[2] == 2);
    SH_CHECK(a.light_color[0] == 3200 && a.light_color[1] == 640);
    SH_CHECK(a.char_light[0] == (3200 * 4096) >> 17);
    const w::ColorScale half{true, 128, 0, 255};
    const w::LightSetup b = w::light_setup(1, 4096, 200, {3200, 640, 64}, 7, half, half);
    SH_CHECK(b.light_color[0] == 3200 * 128 / 255 && b.light_color[1] == 0 && b.light_color[2] == 64);
    SH_CHECK(b.world_tint[0] == 100 * 128 / 255 && b.world_tint[1] == 0);
}

SH_TEST(world_env_params_parse) {
    const auto ok = w::WorldEnvParams::parse(
        R"({"format":"sh-world-env","fog_curve":[0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15],)"
        R"("light":{"axis_bias":10,"axis_span":20,"sample":[1,2,3]}})");
    SH_CHECK(ok);
    SH_CHECK(ok.value().fog_curve[16] == 15 && ok.value().light_axis_span == 20 && ok.value().light_sample[2] == 3);
    SH_CHECK(!w::WorldEnvParams::parse(R"({"format":"other"})"));
    SH_CHECK(!w::WorldEnvParams::parse(
        R"({"format":"sh-world-env","fog_curve":[1],"light":{"axis_bias":1,"axis_span":1,"sample":[0,0,0]}})"));
    SH_CHECK(!w::WorldEnvParams::parse(
        R"({"format":"sh-world-env","fog_curve":[0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15],"light":{"axis_bias":1,"axis_span":0,"sample":[0,0,0]}})"));
}
