// The world atmosphere (engine/world/world_atmosphere.h) on its own state: values worked out by hand from the
// original's behaviour.

#include "engine/world/world_atmosphere.h"

#include "framework/sh_test.h"

namespace {

namespace w = sh::world;

struct Fixture {
    w::WorldEnvParams params;
    w::AtmosphereState state;
    w::WorldAtmosphere atmosphere{state, params};
};

}  // namespace

SH_TEST(world_atmosphere_reset_values) {
    Fixture f;
    f.state.light_mode = 2;
    f.state.world_tint.code = 0x3C;
    f.atmosphere.reset();
    SH_CHECK(f.state.light_mode == 0);
    SH_CHECK(f.state.ambient == 4096);
    SH_CHECK(f.state.world_tint.r == 128 && f.state.world_tint.g == 128 && f.state.world_tint.b == 128);
    SH_CHECK(f.state.world_tint.code == 0x3C);  // the code byte is not the atmosphere's
    SH_CHECK(f.state.fog_enabled == 0);
    SH_CHECK(f.state.fog.color.r == 255 && f.state.fog.color.g == 255 && f.state.fog.color.b == 255);
    // 32 m and 34 m in Q8; the ramp ends at the first power of two past the near distance.
    SH_CHECK(f.state.fog.near_distance == 8192);
    SH_CHECK(f.state.fog.far_distance == 8704);
    SH_CHECK(f.state.fog.depth_shift == 13);
}

SH_TEST(world_atmosphere_fog_distance_scale_and_reapply) {
    Fixture f;
    f.atmosphere.set_fog_distance_scale_pct(200);
    f.atmosphere.set_fog_distance(10 * 4096, 20 * 4096);
    SH_CHECK(f.state.fog.near_distance == 5120);
    SH_CHECK(f.state.fog.far_distance == 10240);
    f.atmosphere.set_fog_distance_scale_pct(100);
    f.atmosphere.reapply_fog_distance();
    SH_CHECK(f.state.fog.near_distance == 2560);
    SH_CHECK(f.state.fog.far_distance == 5120);
}

SH_TEST(world_atmosphere_fog_keep_and_cull_distance) {
    Fixture f;
    f.atmosphere.reset();
    SH_CHECK(f.atmosphere.fog_keep(100) == 256);  // no fog: everything survives
    f.atmosphere.set_fog(true, 10, 20, 30);
    SH_CHECK(f.atmosphere.fog_keep(-5) == 256 - f.state.fog_ramp[0]);
    SH_CHECK(f.atmosphere.fog_keep(1 << 13) == 0);
    SH_CHECK(f.atmosphere.fog_keep((1 << 13) - 1) == 256 - f.state.fog_ramp[127]);

    SH_CHECK(f.atmosphere.fog_cull_distance(false, 0.5f) == 8704);
    SH_CHECK(f.atmosphere.fog_cull_distance(true, 1.0f) == 8704);
    SH_CHECK(f.atmosphere.fog_cull_distance(true, 0.5f) == 17408);
    SH_CHECK(f.atmosphere.fog_cull_distance(true, 0.01f) == 87040);  // the strength never goes below 0.1
}

SH_TEST(world_atmosphere_lighting_and_darkness) {
    Fixture f;
    f.atmosphere.set_lighting(0, 4096, 100, {4096, 2048, 0}, 7, {}, {});
    SH_CHECK(f.state.world_tint.r == 128 && f.state.world_tint.g == 64 && f.state.world_tint.b == 0);
    SH_CHECK(f.state.char_light.r == 128 && f.state.char_light.g == 64 && f.state.char_light.b == 0);
    SH_CHECK(f.state.light_color.m[0][2] == 4096 && f.state.light_color.m[1][0] == 2048 &&
             f.state.light_color.m[2][1] == 0);
    SH_CHECK(f.state.screen_brightness == 7);
    // Without a directional light the darkness is the ambient level (4096 >> 5), capped.
    SH_CHECK(f.atmosphere.darkness_at(0, 0, 0) == 100);
    f.state.light_level_max = 200;
    SH_CHECK(f.atmosphere.darkness_at(123456, 0, -98765) == 128);
}

SH_TEST(world_atmosphere_point_light) {
    Fixture f;
    const w::Vec3s16 dir{0, 4096, 0, 7};
    f.atmosphere.set_point_light(0x1800, 33, nullptr, dir, {4096, 8192, -4096}, {1, 2, 0, 0});
    SH_CHECK(f.state.light_depth_cue == 0x18);
    SH_CHECK(f.state.light_direction.y == 4096 && f.state.light_direction.pad == 7);
    SH_CHECK(f.state.lens_flare_intensity == 33);
    const w::LightFalloffTable t = w::light_falloff_compute(f.params, 0x1800, {0, 4096, 0});
    SH_CHECK(f.state.falloff[1][0].x == t[1][0].x && f.state.falloff[1][1].z == t[1][1].slope);

    // Identity frame at the origin: the direction is scaled by the intensity, the position is in Q8.
    w::ModelFrame frame;
    frame.m[0][0] = frame.m[1][1] = frame.m[2][2] = 4096;
    f.atmosphere.set_model_light(frame);
    SH_CHECK(f.state.local_light_direction.y == 6144);
    SH_CHECK(f.state.local_light_position.x == 256 && f.state.local_light_position.y == 512 &&
             f.state.local_light_position.z == -256);
}

SH_TEST(world_atmosphere_effect_colors) {
    Fixture f;
    f.atmosphere.reset();
    f.state.world_tint = {128, 64, 32, 0x3C};
    f.state.fog.color = {200, 100, 50, 0x38};
    // At depth 0 the ramp is empty: the fog term is black, the tint is untouched at full level.
    const w::FogTint near = f.atmosphere.fog_tint(128, 0);
    SH_CHECK(near.fog.r == 0 && near.fog.g == 0 && near.fog.b == 0 && near.fog.code == 0x38);
    SH_CHECK(near.tint.r == 128 && near.tint.g == 64 && near.tint.b == 32 && near.tint.code == 0x3C);
    // Past the ramp (255 of 256) nearly all of it is fog colour and the tint is nearly gone.
    const w::FogTint far = f.atmosphere.fog_tint(128, 1 << 22);
    SH_CHECK(far.fog.r >= 198 && far.fog.g >= 98 && far.fog.b >= 48);
    SH_CHECK(far.tint.r <= 1 && far.tint.g <= 1 && far.tint.b <= 1);

    const w::Color8 lit = f.atmosphere.faded_tint(0x2C, 128);
    SH_CHECK(lit.r == 128 && lit.g == 64 && lit.b == 32 && lit.code == 0x2C);
    const w::Color8 dark = f.atmosphere.faded_tint(0x2C, 0);
    SH_CHECK(dark.r == 0 && dark.g == 0 && dark.b == 0);
}
