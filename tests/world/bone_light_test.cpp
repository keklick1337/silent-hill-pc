// The light of a character bone (engine/world/bone_light.h).

#include "engine/world/bone_light.h"

#include "engine/world/mesh_prep.h"
#include "framework/sh_test.h"

namespace {

namespace w = sh::world;

struct Fixture {
    w::WorldEnvParams params;
    w::AtmosphereState state;
    w::WorldAtmosphere atmosphere{state, params};
};

}  // namespace

SH_TEST(bone_light_mode0_is_flat_tint) {
    Fixture f;
    f.atmosphere.reset();
    f.state.world_tint = {100, 120, 140, 0};
    f.state.ambient = 4096;  // full map light: the tint itself
    w::BoneLight l = w::bone_light(f.atmosphere, nullptr);
    SH_CHECK(l.ambient[0] == 100 && l.ambient[1] == 120 && l.ambient[2] == 140);
    f.state.ambient = 0;  // no map light: black
    l = w::bone_light(f.atmosphere, nullptr);
    SH_CHECK(l.ambient[0] == 0 && l.ambient[1] == 0 && l.ambient[2] == 0);
    for (int i = 0; i < 9; ++i) {
        SH_CHECK(l.light[i] == 0 && l.color[i] == 0);
    }
}

SH_TEST(bone_light_mode2_scaled_vector) {
    Fixture f;
    f.atmosphere.reset();
    f.state.light_mode = 2;
    f.state.local_light_direction = {2000, -1000, 300, 0};
    f.state.char_light = {10, 20, 30, 0};
    f.state.light_color.m[0][0] = 4096;
    f.state.light_color.m[2][1] = -7;
    const w::BoneLight l = w::bone_light(f.atmosphere, nullptr);
    SH_CHECK(l.light[0] == 1000 && l.light[1] == -500 && l.light[2] == 150);
    for (int i = 3; i < 9; ++i) {
        SH_CHECK(l.light[i] == 0);
    }
    SH_CHECK(l.back[0] == 160 && l.back[1] == 320 && l.back[2] == 480);
    SH_CHECK(l.color[0] == 4096 && l.color[7] == -7);
}

SH_TEST(bone_light_mode1_matches_chara_light) {
    Fixture f;
    f.atmosphere.reset();
    f.state.light_mode = 1;
    f.state.light_level_max = 200;
    f.state.light_depth_cue = -300;
    f.state.local_light_direction = {0, 4096, 0, 0};
    f.state.local_light_position = {50, -400, 120, 0};
    f.state.char_light = {40, 40, 40, 0};
    f.state.world_tint = {128, 128, 128, 0};
    const w::BoneLight l = w::bone_light(f.atmosphere, nullptr);

    w::CharaLightIn in;
    in.light_dir[1] = 4096;
    in.light_pos[0] = 50;
    in.light_pos[1] = -400;
    in.light_pos[2] = 120;
    in.light_cap = 200;
    in.env_color[0] = in.env_color[1] = in.env_color[2] = 40;
    in.tint[0] = in.tint[1] = in.tint[2] = 128;
    in.alpha = 4096;
    in.depth_cue_slope = -300;
    const w::CharaLight ref = w::chara_light(in);
    for (int k = 0; k < 3; ++k) {
        SH_CHECK(l.light[k] == ref.source[k]);
        SH_CHECK(l.back[k] == ref.back[k] * 16);
    }
}
