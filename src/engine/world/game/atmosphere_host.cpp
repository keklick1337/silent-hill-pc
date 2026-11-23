// SPDX-License-Identifier: GPL-3.0-or-later
// The running game's steps on the world atmosphere -- see atmosphere_host.h. Also the atmosphere queries under the
// names the effect drawers call, on the engine's records (a colour is Color8, a model frame ModelFrame).

#include "engine/world/game/atmosphere_host.h"

#include <cstdint>

#include "engine/camera/camera_motion.h"
#include "engine/camera/view_system.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/session.h"
#include "engine/math/geometry_state.h"
#include "engine/world/screen_glow_overlay.h"
#include "render/gpu/render_state.h"
#include "input/camera_style.h"

extern "C" {
// Console overrides (`fl`, `wl`), defined with the map light machine.
extern int g_PcFlashlightColorActive, g_PcWorldLightColorActive;
extern unsigned char g_PcFlashlightColorR, g_PcFlashlightColorG, g_PcFlashlightColorB;
extern unsigned char g_PcWorldLightColorR, g_PcWorldLightColorG, g_PcWorldLightColorB;
// The first-person view: the flashlight follows the eye; the shadow map keeps the real light.
extern int g_PcAltCamApplied;
extern sh::world::Vec3i32 g_PcFpsViewFwd;
extern sh::world::Vec3i32 g_PcFpsEyePos;
extern sh::world::Vec3i32 g_PcFlashlightShadowWorld;
// A script owns the scene (cutscene camera): the game's own light stays.
int Pc_ScriptOwnsScene(void);
// The water effect starts over (the effect's own bridge).
void shLensFlareInit(void);
}

namespace sh::world {

namespace {

namespace cam = sh::camera;

Vec3s16 vec3s16_of(const sh::math::Vec3s& v, std::int16_t pad) { return {v.x, v.y, v.z, pad}; }

}  // namespace

void world_env_init() {
    screen_glow_init();
    shLensFlareInit();
    sh::math::geometry().set_far_color(0, 0, 0);
    atmosphere().reset();
}

void frame_light_set(std::uint8_t mode, std::int32_t ambient, std::uint8_t cap, std::int32_t tint_r,
                     std::int32_t tint_g, std::int32_t tint_b, std::int32_t brightness) {
    const ColorScale light{g_PcFlashlightColorActive != 0, g_PcFlashlightColorR, g_PcFlashlightColorG,
                           g_PcFlashlightColorB};
    const ColorScale world{g_PcWorldLightColorActive != 0, g_PcWorldLightColorR, g_PcWorldLightColorG,
                           g_PcWorldLightColorB};
    atmosphere().set_lighting(mode, ambient, cap, {tint_r, tint_g, tint_b}, brightness, light, world);
}

void point_light_set(std::int32_t intensity, std::int16_t lens_flare, void* direction_bone, void* position_bone,
                     const Vec3s16& rotation, std::int32_t x, std::int32_t y, std::int32_t z, void* water_zones) {
    const AtmosphereState& state = atmosphere().state();

    // The direction and angle records keep their padding as the game left it.
    Vec3s16 direction = state.light_direction;
    if (direction_bone == nullptr) {
        direction = rotation;
    } else {
        const cam::Matrix frame = cam::frame_world(static_cast<cam::FrameRecord*>(direction_bone));
        direction = vec3s16_of(sh::math::geometry().apply_short(sh::math::transform_of(frame), {rotation.x, rotation.y,
                                                                                          rotation.z}),
                               direction.pad);
    }

    Vec3i32 position{x, y, z};
    if (position_bone != nullptr) {
        const cam::Matrix frame = cam::frame_world(static_cast<cam::FrameRecord*>(position_bone));
        // The offset turns in the frame at Q8.
        const sh::math::Vec3s local{static_cast<std::int16_t>(x >> 4), static_cast<std::int16_t>(y >> 4),
                                   static_cast<std::int16_t>(z >> 4)};
        const sh::math::Vec3i turned = sh::math::geometry().apply(sh::math::transform_of(frame), local);
        position = {(turned.x + frame.t[0]) << 4, (turned.y + frame.t[1]) << 4, (turned.z + frame.t[2]) << 4};
    }

    // First person: the light comes from the eye along the view, but only while that camera is the view (a script
    // owning the scene keeps the game's own light; the dark Cybil scenes went black otherwise).
    const int fps_view = sh::input::camera_styles().first_person() && g_PcAltCamApplied;
    sh::render::flashlight().fps_mode = fps_view;
    g_PcFlashlightShadowWorld = position;
    if (fps_view && sh::game::session().system.environment.flashlight_on) {
        direction.x = static_cast<std::int16_t>(g_PcFpsViewFwd.x);
        direction.y = static_cast<std::int16_t>(g_PcFpsViewFwd.y);
        direction.z = static_cast<std::int16_t>(g_PcFpsViewFwd.z);
        position = g_PcFpsEyePos;
    }

    Vec3s16 angles = state.light_angles;
    const std::int16_t dir[3] = {direction.x, direction.y, direction.z};
    cam::angles_from_direction(dir, angles.x, angles.y);
    angles.z = 0;

    atmosphere().set_point_light(intensity, lens_flare, water_zones, direction, position, angles);
}

}  // namespace sh::world

namespace sw = sh::world;

// The effect drawers read the atmosphere under the game's record name:
// a view of the engine's storage, with the record's layout.
extern "C" {
extern sw::AtmosphereState& g_WorldEnvWork;
extern sw::AtmosphereState* const g_WorldEnvWorkPtr;
sw::AtmosphereState& g_WorldEnvWork = sw::detail::g_atmosphere_state;
sw::AtmosphereState* const g_WorldEnvWorkPtr = &sw::detail::g_atmosphere_state;
}

namespace {

// Lighting a model leaves its transposed matrix in the transform unit.
void load_model_light_registers(const sw::ModelFrame& frame) {
    sh::math::geometry().load_rotation_transposed(sh::math::transform_of(frame).rot);
}

}  // namespace

extern "C" {

std::int32_t Pc_FogFarDist(void) {
    return sw::atmosphere().fog_cull_distance(g_PcConfig.culling != 0, g_PcConfig.fogStrength);
}

int Pc_BloodFogKeep(std::int32_t z) { return sw::atmosphere().fog_keep(z); }

std::int32_t WorldEnv_LightIntensityGet(sw::Vec3s16* angles) {
    const sw::AtmosphereState& s = sw::atmosphere().state();
    *angles = s.light_angles;
    return s.light_intensity;
}

std::uint8_t WorldEnv_DarknessAtGet(std::int32_t x, std::int32_t y, std::int32_t z) {
    return sw::atmosphere().darkness_at(x, y, z);
}

void WorldEnv_ColorFogApply(sw::Color8* fog, sw::Color8* tint, std::uint8_t level, std::int32_t depth) {
    const sw::FogTint out = sw::atmosphere().fog_tint(level, depth);
    *fog = out.fog;
    *tint = out.tint;
}

void WorldEnv_ColorFadeApply(sw::Color8* color, std::uint8_t amount) {
    *color = sw::atmosphere().faded_tint(color->code, amount);
}

// Every caller hands the point light itself, so only the model frame matters.
void WorldEnv_RelLightPosCalc(sw::ModelFrame* frame, std::int32_t, const void*, const void*) {
    load_model_light_registers(*frame);
    sw::atmosphere().set_model_light(*frame);
}

// A point and normal as three shorts each.
struct Short3 {
    std::int16_t x, y, z;
};

std::uint8_t WorldEnv_FadeAmountGet(const Short3* point, const Short3* normal, sw::ModelFrame* frame) {
    load_model_light_registers(*frame);
    const sw::WorldAtmosphere::Fade fade =
        sw::atmosphere().fade_amount({point->x, point->y, point->z}, {normal->x, normal->y, normal->z}, *frame);
    // The depth cue and the fade's rows stay loaded for the code that follows.
    sh::math::geometry().set_depth_cue_slope(sw::atmosphere().state().light_depth_cue);
    sh::math::geometry().set_depth_cue_base(0);
    sh::math::geometry().set_translation({});
    sh::math::Transform rows{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            rows.rot.m[row][col] = fade.rotation[row][col];
        }
    }
    sh::math::geometry().load_rotation(rows);
    return fade.amount;
}

}  // extern "C"
