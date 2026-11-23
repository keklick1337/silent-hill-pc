// The map environment blend (engine/world/map_env.h) against values worked out by hand from the original's
// behaviour: progress alpha, weighted average, fog parameters, zone blends and the config file.

#include "engine/world/map_env.h"

#include "framework/sh_test.h"

namespace {

namespace w = sh::world;

w::EnvZone zone(std::uint8_t light_mode, std::int16_t alpha, std::uint8_t fog_mode, std::int32_t fog) {
    w::EnvZone z;
    w::EnvInfo i;
    i.light_mode = light_mode;
    i.alpha = alpha;
    i.fog_mode = fog_mode;
    i.fog_distance = fog;
    w::fog_params(z, i);
    return z;
}

}  // namespace

SH_TEST(map_env_progress_alpha_clamps_and_handles_either_order) {
    SH_CHECK(w::progress_alpha(50, 0, 100) == 2048);
    SH_CHECK(w::progress_alpha(-5, 0, 100) == 0);
    SH_CHECK(w::progress_alpha(500, 0, 100) == 4096);
    SH_CHECK(w::progress_alpha(25, 100, 0) == 3072);
    SH_CHECK(w::progress_alpha(7, 7, 7) == 4096);
    // A span wider than 19 bits keeps the numerator in 32 bits by dropping low bits first.
    SH_CHECK(w::progress_alpha(1 << 20, 0, 1 << 21) == 2048);
}

SH_TEST(map_env_weighted_average) {
    SH_CHECK(w::weighted_average(100, 200, 2048) == 150);
    SH_CHECK(w::weighted_average(100, 200, 0) == 100);
    SH_CHECK(w::weighted_average(100, 200, 4096) == 200);
    SH_CHECK(w::weighted_average(-3, 0, 2048) == -2);  // floor, as the shift rounds
}

SH_TEST(map_env_fog_params_follow_flags_and_mode) {
    w::EnvZone z;
    w::EnvInfo i;
    i.flags = w::kEnvFlagBrightness | w::kEnvFlagLensFlare;
    i.fog_mode = 1;
    i.fog_distance = 5000;
    w::fog_params(z, i);
    SH_CHECK(z.brightness == 4096 && z.lens_flare == 4096 && z.fog_distance == 5000);
    i.flags = 0;
    i.fog_mode = 2;
    w::fog_params(z, i);
    SH_CHECK(z.brightness == 0 && z.lens_flare == 0 && z.fog_distance == 0);
    i.fog_mode = 9;
    z.fog_distance = 77;
    w::fog_params(z, i);
    SH_CHECK(z.fog_distance == 77);  // an unknown mode leaves the distance
}

SH_TEST(map_env_fog_distance_table) {
    const w::MapEnvParams p;
    w::EnvInfo i;
    i.fog_distance = 12345;
    SH_CHECK(w::fog_distance(p, i, true, true) == 12345);
    SH_CHECK(w::fog_distance(p, i, false, false) == 81920);
    i.alpha = 0;
    SH_CHECK(w::fog_distance(p, i, false, true) == 7168);
    i.alpha = 1024;  // halfway between the first two entries
    SH_CHECK(w::fog_distance(p, i, false, true) == (7168 + 24576) / 2);
    i.alpha = 8192;
    SH_CHECK(w::fog_distance(p, i, false, true) == 61440);
    i.alpha = -4;
    SH_CHECK(w::fog_distance(p, i, false, true) == 7168);
}

SH_TEST(map_env_blend_zone_midpoint_and_light_cross_fade) {
    const w::MapEnvParams p;
    w::EnvZone a = zone(0, 100, 0, 4096);
    w::EnvZone b = zone(0, 300, 0, 12288);
    a.info.fog_color = {0, 0, 0};
    b.info.fog_color = {200, 100, 50};
    w::EnvZone out;
    w::blend_zone(p, out, a, b, 2048);
    SH_CHECK(out.info.alpha == 200);
    SH_CHECK(out.info.fog_distance == 8192);
    SH_CHECK(out.info.fog_color.r == 100 && out.info.fog_color.g == 50 && out.info.fog_color.b == 25);
    SH_CHECK(out.fog_distance == 8192);

    // Light mode 1 -> 2: before the pivot the old layer fades out, mode stays 1.
    const w::EnvZone m1 = zone(1, 1000, 1, 0);
    const w::EnvZone m2 = zone(2, 2000, 1, 0);
    w::blend_zone(p, out, m1, m2, 1000);
    SH_CHECK(out.info.light_mode == 1);
    SH_CHECK(out.info.alpha == w::weighted_average(1000, 0, (1000 * 4915) >> 12));
    w::blend_zone(p, out, m1, m2, 4096);
    SH_CHECK(out.info.light_mode == 2 && out.info.alpha == 2000);
}

SH_TEST(map_env_screen_tint_flag_follows_the_colours) {
    const w::MapEnvParams p;
    w::EnvZone a = zone(0, 0, 0, 0);
    w::EnvZone b = zone(0, 0, 0, 0);
    b.info.screen_tint_on = 1;
    b.info.screen_tint_sub = {40, 0, 0};
    w::EnvZone out;
    w::blend_zone(p, out, a, b, 4096);
    SH_CHECK(out.info.screen_tint_on == 1 && out.info.screen_tint_sub.r == 40);
    w::blend_zone(p, out, a, a, 4096);
    SH_CHECK(out.info.screen_tint_on == 0);
}

SH_TEST(map_env_params_parse_and_reject) {
    const char* good = R"({"format":"sh-map-effects","version":1,
        "light_mode_cross_fade":{"to_dense_pivot":3000,"from_dense_pivot":600,"long_rate":5000},
        "fog_by_alpha":{"alpha_span":4096,"distances":[1,2,3,4,5],"open_fog_distance":99}})";
    auto p = w::MapEnvParams::parse(good);
    SH_CHECK(static_cast<bool>(p));
    SH_CHECK(p.value().to_dense_pivot == 3000 && p.value().fog_by_alpha[4] == 5 && p.value().open_fog_distance == 99);
    SH_CHECK(!w::MapEnvParams::parse(R"({"format":"other"})"));
    SH_CHECK(!w::MapEnvParams::parse(R"({"format":"sh-map-effects","light_mode_cross_fade":{},"fog_by_alpha":{}})"));
    SH_CHECK(!w::MapEnvParams::parse("not json"));
}

// The same blend on the session's environment records: the whole preset is taken over, only named fields come back.
SH_TEST(map_env_state_matches_zone_blend) {
    sh::game::EnvironmentPreset preset{};
    preset.light_mode = 1;
    preset.spot_light_intensity = 1200;
    preset.fog_mode = 1;
    preset.fog_distance = 40000;
    preset.fog_color = {10, 20, 30, 0x2C};
    preset.point_light_tint.x = 7;
    preset.rain_color.x = 9;

    sh::game::EnvironmentState a{};
    w::set_preset_state(a, preset);
    SH_CHECK(a.preset.point_light_tint.x == 7 && a.preset.rain_color.x == 9 && a.preset.fog_color.x == 0x2C);
    w::EnvZone expect;
    w::fog_params(expect, w::env_info_of(preset));
    SH_CHECK(a.fog_distance == expect.fog_distance && a.lens_flare_intensity == expect.lens_flare &&
             a.brightness_intensity == expect.brightness);

    sh::game::EnvironmentState b = a;
    b.preset.light_mode = 2;
    b.preset.spot_light_intensity = 0;
    b.preset.fog_color = {200, 100, 0, 0x2C};
    const w::MapEnvParams params;
    sh::game::EnvironmentState out{};
    out.preset.snow_color.x = 5;
    w::EnvZone zout = w::env_zone_of(out);
    w::blend_zone(params, zout, w::env_zone_of(a), w::env_zone_of(b), 1500);
    w::blend_state(params, out, a, b, 1500);
    const w::EnvZone got = w::env_zone_of(out);
    SH_CHECK(got.info.alpha == zout.info.alpha && got.info.light_mode == zout.info.light_mode);
    SH_CHECK(got.info.fog_color.r == zout.info.fog_color.r && got.info.fog_color.g == zout.info.fog_color.g);
    SH_CHECK(got.fog_distance == zout.fog_distance && out.preset.snow_color.x == 5);
}
