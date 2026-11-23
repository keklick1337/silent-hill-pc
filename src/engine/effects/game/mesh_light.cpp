// SPDX-License-Identifier: GPL-3.0-or-later
// The character light set-up and the scaled light vector of the billboard drawer (engine/world/mesh_prep.h,
// docs/RENDER.md section 5.3): the engine computes on plain values; this keeps the drawer's scratch layout
// and leaves the light in the geometry state, where the billboard drawer still takes it.

#include <cstdint>
#include <cstring>

#include "engine/math/precise_capture.h"
#include "engine/math/geometry_state.h"
#include "engine/world/mesh_prep.h"
#include "engine/world/world_atmosphere.h"


namespace {

namespace sw = sh::world;

// The drawer's light block (in its scratch): the projected direction, the depth cue and the rotation.
constexpr std::size_t kScreenOffset = 0;
constexpr std::size_t kDepthCueOffset = 4;
constexpr std::size_t kRotationOffset = 8;

sh::math::GeometryState& geometry() { return sh::math::geometry(); }

// One directional light: the source vector as the first row of the light matrix.
void load_light(const std::int16_t (&source)[3]) { geometry().load_light_direction({source[0], source[1], source[2]}); }

}  // namespace

extern "C" {

void Mesh_DirLightMatrixSet(void* block, std::int32_t alpha) {
    const sw::AtmosphereState& a = sw::atmosphere().state();
    sw::CharaLightIn in;
    in.light_dir[0] = a.local_light_direction.x;
    in.light_dir[1] = a.local_light_direction.y;
    in.light_dir[2] = a.local_light_direction.z;
    in.light_pos[0] = a.local_light_position.x;
    in.light_pos[1] = a.local_light_position.y;
    in.light_pos[2] = a.local_light_position.z;
    in.light_cap = a.light_level_max;
    in.env_color[0] = a.char_light.r;
    in.env_color[1] = a.char_light.g;
    in.env_color[2] = a.char_light.b;
    in.tint[0] = a.world_tint.r;
    in.tint[1] = a.world_tint.g;
    in.tint[2] = a.world_tint.b;
    in.alpha = alpha;
    in.depth_cue_slope = geometry().screen().dqa;
    in.depth_cue_base = geometry().screen().dqb;

    const sw::CharaLight out = sw::chara_light(in);

    auto* s = static_cast<unsigned char*>(block);
    std::memcpy(s + kRotationOffset, out.rot, sizeof(out.rot));
    const std::int16_t screen[2] = {out.screen.x, out.screen.y};
    std::memcpy(s + kScreenOffset, screen, sizeof(screen));
    std::memcpy(s + kDepthCueOffset, &out.depth_cue, sizeof(out.depth_cue));

    // The rotation (as the scratch holds it) and a zero translation stay loaded for the callers that follow.
    sh::math::Matrix3 rot;
    std::memcpy(&rot.m, s + kRotationOffset, sizeof(rot.m));
    geometry().set_rotation(rot);
    sh::math::capture::rotation_loaded(s + kRotationOffset);
    geometry().set_translation({});

    load_light(out.source);
    geometry().set_background_color(out.back[0], out.back[1], out.back[2]);
}

void Mesh_LightVecScale(void* block, std::int32_t scale) {
    (void)block;
    const sw::AtmosphereState& a = sw::atmosphere().state();
    const std::int16_t dir[3] = {a.local_light_direction.x, a.local_light_direction.y, a.local_light_direction.z};
    const std::uint8_t env[3] = {a.char_light.r, a.char_light.g, a.char_light.b};
    const sw::ScaledLight out = sw::scaled_light(dir, env, scale);
    load_light(out.source);
    geometry().set_background_color(out.back[0], out.back[1], out.back[2]);
}

}  // extern "C"
