// SPDX-License-Identifier: GPL-3.0-or-later
// The map light machine (engine/effects/map_lighting.h) and the entry points the game code and the scripts call.

#include "engine/effects/map_lighting.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "engine/app/map_info_tables.h"
#include "engine/camera/camera_types.h"
#include "engine/camera/view_space.h"
#include "engine/camera/view_system.h"
#include "engine/config/map_env_config.h"
#include "engine/config/pc_config.h"
#include "engine/effects/effect_world.h"
#include "engine/effects/map_lighting_config.h"
#include "engine/effects/named_effects_host.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/save_record.h"
#include "engine/gconfig/map_info.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_runtime.h"
#include "engine/math/fixed_geometry.h"
#include "engine/world/map_env.h"
#include "engine/world/screen_glow.h"
#include "engine/world/world_atmosphere.h"
#include "engine/world/world_env.h"
#include "engine/world/game/atmosphere_host.h"
#include "engine/world/screen_glow_overlay.h"
#include "engine/mods/data_reload.h"

// Entry points of other modules this machine drives.
extern "C" {
std::int32_t Vc_LensFlareTypeGet(void);
void WorldGfx_HandEffectDraw(void);

// Console colour overrides (`fl`/`flashlight`, `wl`/`worldlight`): each channel scales the map's light colour and
// the world tint.
int g_PcFlashlightColorActive = 0;
unsigned char g_PcFlashlightColorR = 255, g_PcFlashlightColorG = 255, g_PcFlashlightColorB = 255;
int g_PcWorldLightColorActive = 0;
unsigned char g_PcWorldLightColorR = 255, g_PcWorldLightColorG = 255, g_PcWorldLightColorB = 255;
}

namespace sh::effects {

namespace {

namespace sw = sh::world;
namespace sg = sh::game;

constexpr std::uint32_t kFlashlightOffToggle = 1u << 1;  // the save's item toggle: the flashlight was switched off
constexpr std::uint8_t kFlashToPreset = 1u << 0;
constexpr std::uint8_t kFlashBoost = 1u << 1;
constexpr std::uint8_t kFogFollowsFlashlight = 3;
constexpr std::int32_t kOne = 4096;

const MapLightingConfig& config() {
    static std::optional<MapLightingConfig> cfg;
    static std::uint32_t epoch = sh::mods::kNeverRead;
    if (sh::mods::data_stale(epoch)) {
        cfg.reset();
        std::string source;
        Result<MapLightingConfig> loaded = load_map_lighting(named::gamedata_dir(), named::mods_dir(), &source);
        if (loaded) {
            cfg.emplace(std::move(loaded.value()));
            SH_LOG("[FX] map lighting from %s", source.c_str());
        } else {
            SH_ERR("[FX] %s - map light presets and rigs are zero", loaded.error().c_str());
            cfg.emplace();
        }
    }
    return *cfg;
}

std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

void set_rig(const LightRig& rig) {
    sg::SystemBlock& s = sg::session().system;
    s.light_intensity = rig.intensity;
    s.light_bone = &s.characters.player_bones[rig.bone];
    s.lens_flare_bone = &s.characters.player_bones[rig.flare_bone];
    s.light_position = {rig.position[0], rig.position[1], rig.position[2]};
    s.light_rotation = {rig.rotation[0], rig.rotation[1], rig.rotation[2]};
}

void set_flashlight_toggle(bool on) {
    std::uint32_t& toggles = sg::save_record().item_toggles;
    toggles = on ? toggles & ~kFlashlightOffToggle : toggles | kFlashlightOffToggle;
}

}  // namespace

void MapLighting::assign_for_map() {
    const sh::app::MapTypeRecord* info = sh::app::map_type_record(sh::maps::map_runtime().map_type());
    const std::uint8_t flags = info != nullptr ? info->flags : 0;
    bool active_chunk = false;
    if (flags & sh::gconfig::kMapInterior) {
        active_chunk = (flags & (sh::gconfig::kMapOneActiveChunk | sh::gconfig::kMapTwoActiveChunks)) != 0;
    }
    const PresetPair p = config().presets_for(sh::maps::map_runtime().light_mode(), active_chunk);
    set_now(p[0], p[1]);
}

void MapLighting::transition(std::int32_t to_off, std::int32_t to_on, WatchedValue type, const void* value,
                             std::int32_t from, std::int32_t to) {
    const sg::EnvironmentPreset* presets = effect_world::environment_presets();
    mix_.flashlight_unavailable = to_off == to_on ? 1 : 0;
    mix_.progress_value = value;
    mix_.progress_type = static_cast<std::int32_t>(type);
    mix_.progress_start = from;
    mix_.progress_end = to;
    mix_.transition_from[0] = mix_.transition[0];
    mix_.transition_from[1] = mix_.transition[1];
    sw::set_preset_state(mix_.transition_to[0], presets[to_off]);
    sw::set_preset_state(mix_.transition_to[1], presets[to_on]);
}

void MapLighting::set_now(std::int32_t to_off, std::int32_t to_on) {
    transition(to_off, to_on, WatchedValue::None, nullptr, 0, 0);
    update();
}

std::uint32_t MapLighting::watched_value() const {
    const void* p = mix_.progress_value;
    switch (static_cast<WatchedValue>(mix_.progress_type)) {
        case WatchedValue::S8: return static_cast<std::uint32_t>(*static_cast<const std::int8_t*>(p));
        case WatchedValue::U8: return *static_cast<const std::uint8_t*>(p);
        case WatchedValue::S16: return static_cast<std::uint32_t>(*static_cast<const std::int16_t*>(p));
        case WatchedValue::U16: return *static_cast<const std::uint16_t*>(p);
        case WatchedValue::S32: return static_cast<std::uint32_t>(*static_cast<const std::int32_t*>(p));
        default: return 0;
    }
}

void MapLighting::update() {
    sg::SystemBlock& sys = sg::session().system;
    const sw::MapEnvParams& params = sw::map_env_params();

    mix_.flashlight_intensity = static_cast<std::int16_t>(sw::flashlight_intensity_step(
        mix_.flashlight_intensity, mix_.flashlight_on != 0, sh::game_state::delta_time()));
    const int lit = mix_.flashlight_intensity != 0 ? 1 : 0;

    if (mix_.transition_to[lit].preset.fog_mode == kFogFollowsFlashlight) {
        // The fog ends where the flashlight points: the light's offset in its bone, seen from the eye.
        const sh::camera::Matrix m = sh::camera::view_matrix(
            sh::camera::view_system().world_screen(),
            sh::camera::frame_world(static_cast<sh::camera::FrameRecord*>(const_cast<void*>(sys.light_bone))));
        math::Matrix3 rot;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) rot.m[r][c] = m.m[r][c];
        }
        const math::Vec3i v = math::apply_matrix_lv(rot, {sys.light_position.x, sys.light_position.y, sys.light_position.z});
        effect_world::leave_rotation(rot);
        mix_.transition_to[lit].fog_distance = v.z + wrap_mul(m.t[2], 16);
    }

    if (mix_.progress_type == static_cast<std::int32_t>(WatchedValue::None)) {
        mix_.transition[0] = mix_.transition_to[0];
        mix_.transition[1] = mix_.transition_to[1];
    } else {
        const std::int32_t weight = sw::progress_alpha(static_cast<std::int32_t>(watched_value()),
                                                       mix_.progress_start, mix_.progress_end);
        sw::blend_state(params, mix_.transition[0], mix_.transition_from[0], mix_.transition_to[0], weight);
        sw::blend_state(params, mix_.transition[1], mix_.transition_from[1], mix_.transition_to[1], weight);
        if (weight >= kOne) mix_.progress_type = static_cast<std::int32_t>(WatchedValue::None);
    }

    sw::blend_state(params, mix_.current, mix_.transition[0], mix_.transition[1], mix_.flashlight_intensity);

    sg::EnvironmentState& cur = mix_.current;
    if (mix_.flash_effect != 0) {
        const std::uint8_t flags = cur.preset.flags;
        mix_.flash_effect = 0;
        if (flags & kFlashToPreset) {
            sw::set_preset_state(cur, effect_world::environment_presets()[config().flash_preset]);
        } else if (flags & kFlashBoost) {
            cur.preset.spot_light_intensity =
                static_cast<std::int16_t>(cur.preset.spot_light_intensity + config().flash_boost);
        }
    }

    const sw::AtmosphereState& atm = sw::atmosphere().state();
    mix_.visibility_distance =
        sw::fog_distance(params, sw::env_info_of(cur.preset), atm.fog_enabled != 0, atm.light_mode == 1);
    apply_to_atmosphere(cur);

    const sw::LensFlare flare =
        sw::lens_flare_get(cur.preset.spot_light_intensity, cur.preset.light_mode,
                           static_cast<std::uint32_t>(Vc_LensFlareTypeGet()),
                           {sys.light_rotation.x, sys.light_rotation.y, sys.light_rotation.z});
    const sw::Vec3s16 rotation{flare.dir[0], flare.dir[1], flare.dir[2], 0};
    sw::point_light_set(wrap_mul(flare.alpha, sys.light_intensity) >> 12, cur.lens_flare_intensity,
                        flare.follows_bone ? const_cast<void*>(sys.lens_flare_bone) : nullptr,
                        const_cast<void*>(sys.light_bone), rotation, sys.light_position.x, sys.light_position.y,
                        sys.light_position.z, effect_world::map_water_zones());
    sw::atmosphere().set_fog_intensity(cur.fog_distance);

    if (cur.preset.flags & sw::kEnvFlagHandEffect) WorldGfx_HandEffectDraw();
}

void MapLighting::apply_to_atmosphere(const sg::EnvironmentState& state) {
    const sg::EnvironmentPreset& p = state.preset;
    const std::int32_t brightness =
        sw::world_brightness(state.brightness_intensity, sg::session().game.settings.brightness,
                             g_PcConfig.brightnessLevelBias);
    const sw::ColorScale light{g_PcFlashlightColorActive != 0, g_PcFlashlightColorR, g_PcFlashlightColorG,
                               g_PcFlashlightColorB};
    const sw::ColorScale world{g_PcWorldLightColorActive != 0, g_PcWorldLightColorR, g_PcWorldLightColorG,
                               g_PcWorldLightColorB};
    sw::WorldAtmosphere& atm = sw::atmosphere();
    atm.set_lighting(p.light_mode, p.world_light_intensity, p.light_level_max,
                     {p.world_tint[0], p.world_tint[1], p.world_tint[2]}, brightness, light, world);
    atm.set_fog(p.fog_mode != 0, p.fog_color.r, p.fog_color.g, p.fog_color.b);
    atm.set_fog_distance(p.fog_distance, p.fog_distance + kOne);
    atm.state().tint_light_overlap = p.tint_light_overlap;
    if (p.tint_light_overlap != 0) {
        sw::screen_glow_overlay().set_tint(p.point_light_tint.r, p.point_light_tint.g, p.point_light_tint.b,
                                           p.world_tint_color.r, p.world_tint_color.g, p.world_tint_color.b);
    }
}

void MapLighting::set_flashlight(bool on) {
    mix_.flashlight_on = on ? 1 : 0;
    set_flashlight_toggle(on);
}

void MapLighting::toggle_flashlight() {
    mix_.flashlight_on ^= 1;
    set_flashlight_toggle(mix_.flashlight_on == 1);
}

void MapLighting::use_flashlight_rig() { set_rig(config().flashlight); }
void MapLighting::use_load_screen_rig() { set_rig(config().load_screen); }

sg::Rgbx MapLighting::snow_color() const { return mix_.transition[mix_.flashlight_on].preset.snow_color; }
sg::Rgbx MapLighting::rain_color() const { return mix_.transition[mix_.flashlight_on].preset.rain_color; }

}  // namespace sh::effects

namespace fx = sh::effects;

extern "C" {

void Gfx_MapEffectsAssign(void) { fx::map_lighting().assign_for_map(); }

void Game_SpotlightLoadScreenAttribsFix(void) { fx::map_lighting().use_load_screen_rig(); }
void Game_FlashlightAttributesFix(void) { fx::map_lighting().use_flashlight_rig(); }
void Game_TurnFlashlightOn(void) { fx::map_lighting().set_flashlight(true); }
void Game_TurnFlashlightOff(void) { fx::map_lighting().set_flashlight(false); }
void Game_FlashlightToggle(void) { fx::map_lighting().toggle_flashlight(); }

void Gfx_MapInitMapEffectsUpdate(std::int32_t preset0, std::int32_t preset1) {
    fx::map_lighting().set_now(preset0, preset1);
}

void Gfx_LoadScreenMapEffectsUpdate(std::int32_t preset0, std::int32_t preset1) {
    fx::map_lighting().set_now(preset0, preset1);
}

void Gfx_MapEffectsUpdate(std::int32_t preset0, std::int32_t preset1, int value_type, void* value, std::int32_t from,
                          std::int32_t to) {
    fx::map_lighting().transition(preset0, preset1, static_cast<fx::WatchedValue>(value_type), value, from, to);
}

void Gfx_FlashlightUpdate(void) { fx::map_lighting().update(); }

}  // extern "C"
