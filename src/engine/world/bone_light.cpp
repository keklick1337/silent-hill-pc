// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/bone_light.h"

#include "engine/world/mesh_prep.h"

namespace sh::world {

namespace {

// Characters are lit at full opacity; a fading character is faded by the light parameters its drawer sets.
constexpr std::int32_t kFullAlpha = 4096;

}  // namespace

BoneLight bone_light(WorldAtmosphere& atmosphere, const ModelFrame* bone) {
    const AtmosphereState& s = atmosphere.state();
    BoneLight out;
    if (bone != nullptr && s.light_mode != 0) {
        atmosphere.set_model_light(*bone);
    }
    if (s.light_mode == 0) {
        const std::uint8_t tint[3] = {s.world_tint.r, s.world_tint.g, s.world_tint.b};
        // The far colour the blend goes toward is black: the game sets it once at start-up and never again.
        const math::Rgb c = ambient_color(tint, s.ambient, kFullAlpha, math::Vec3i{});
        out.ambient[0] = c.r;
        out.ambient[1] = c.g;
        out.ambient[2] = c.b;
        return out;
    }

    std::int16_t source[3] = {};
    std::int32_t back[3] = {};
    const std::uint8_t env[3] = {s.char_light.r, s.char_light.g, s.char_light.b};
    if (s.light_mode == 1) {
        CharaLightIn in;
        in.light_dir[0] = s.local_light_direction.x;
        in.light_dir[1] = s.local_light_direction.y;
        in.light_dir[2] = s.local_light_direction.z;
        in.light_pos[0] = s.local_light_position.x;
        in.light_pos[1] = s.local_light_position.y;
        in.light_pos[2] = s.local_light_position.z;
        in.light_cap = s.light_level_max;
        in.env_color[0] = env[0];
        in.env_color[1] = env[1];
        in.env_color[2] = env[2];
        in.tint[0] = s.world_tint.r;
        in.tint[1] = s.world_tint.g;
        in.tint[2] = s.world_tint.b;
        in.alpha = kFullAlpha;
        // The depth cue of the light probe is the one the light installs with its colour matrix (base zero).
        in.depth_cue_slope = static_cast<std::int16_t>(s.light_depth_cue);
        in.depth_cue_base = 0;
        const CharaLight l = chara_light(in);
        for (int k = 0; k < 3; ++k) {
            source[k] = l.source[k];
            back[k] = l.back[k];
        }
    } else {
        const std::int16_t dir[3] = {s.local_light_direction.x, s.local_light_direction.y,
                                     s.local_light_direction.z};
        const ScaledLight l = scaled_light(dir, env, kFullAlpha);
        for (int k = 0; k < 3; ++k) {
            source[k] = l.source[k];
            back[k] = l.back[k];
        }
    }
    for (int k = 0; k < 3; ++k) {
        out.light[k] = source[k];
        out.back[k] = back[k] * 16;
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.color[r * 3 + c] = s.light_color.m[r][c];
        }
    }
    return out;
}

}  // namespace sh::world
