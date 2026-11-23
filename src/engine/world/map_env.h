// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Map environment blending (docs/RENDER.md section 5.3): the fog, light and screen tint of a map zone, and
// the blends between two zones (flashlight on/off, a preset change, the transition by a watched value). Pure
// integer math on engine structs; the *_state functions work in place on the session's environment mix
// (sh::game::EnvironmentState, src/engine/game_state/session.h).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "engine/game_state/session.h"
#include "formats/common/status.h"

namespace sh::world {

struct EnvColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

// Bits of EnvInfo::flags.
constexpr std::uint8_t kEnvFlagBit0 = 1u << 0;     // light level follows a two-zone cross-fade that starts late
constexpr std::uint8_t kEnvFlagBit1 = 1u << 1;
constexpr std::uint8_t kEnvFlagBrightness = 1u << 2;  // the brightness layer is shown
constexpr std::uint8_t kEnvFlagHandEffect = 1u << 3;  // the hand light effect is drawn
constexpr std::uint8_t kEnvFlagLensFlare = 1u << 4;   // the flashlight lens flare is shown

// One preset of a zone. Q12 values unless noted.
struct EnvInfo {
    std::uint8_t flags = 0;
    std::uint8_t light_level = 0;   // byte, not blended as a Q12 (see blend_zone)
    std::uint8_t light_mode = 0;    // 0 off, 1 and 2 are cross-faded through the grey layer
    std::int16_t alpha = 0;         // grey layer laid over characters and the scenery
    std::int16_t tint_intensity = 0;
    std::int16_t tint_r = 0;
    std::int16_t tint_g = 0;
    std::int16_t tint_b = 0;
    std::uint8_t fog_mode = 0;      // 0 and 1 plain fog, 2 none, 3 follows the flashlight
    std::int32_t fog_distance = 0;
    EnvColor fog_color;
    std::uint8_t screen_tint_on = 0;
    EnvColor screen_tint_add;
    EnvColor screen_tint_sub;       // subtractive
    EnvColor particle_color_a;      // only the first channel is used, for the snow transparency
    EnvColor particle_color_b;
};

// A preset together with the values derived from it by fog_params().
struct EnvZone {
    EnvInfo info;
    std::int16_t lens_flare = 0;
    std::int16_t brightness = 0;
    std::int32_t fog_distance = 0;  // what the fog code reads; follows the flashlight in mode 3
};

// Numbers of the blend, from gamedata/config/map_effects.json (a mod replaces the file whole).
struct MapEnvParams {
    // Light mode 1 -> 2 and 2 -> 1: the grey layer fades out over the first part of the weight, the new one fades
    // in over the rest. pivot is where the first part ends (Q12), rate the speed-up of the longer part (Q12).
    std::int32_t to_dense_pivot = 3413;
    std::int32_t from_dense_pivot = 682;
    std::int32_t long_rate = 4915;
    // Fog distance from the grey layer alpha when the fog is off in light mode 1: distances in Q12 at even steps
    // of alpha over 0..alpha_span, and the distance when light mode is something else.
    std::array<std::int32_t, 5> fog_by_alpha{7168, 24576, 38912, 51200, 61440};
    std::int32_t alpha_span = 8192;
    std::int32_t open_fog_distance = 81920;

    static Result<MapEnvParams> parse(std::string_view json);
};

// 0..Q12(1) position of `value` between `from` and `to` (either order); Q12(1) when they are equal.
std::int32_t progress_alpha(std::int32_t value, std::int32_t from, std::int32_t to);
// a * (1 - weight) + b * weight.
std::int32_t weighted_average(std::int32_t a, std::int32_t b, std::int32_t weight);
// Fills the values of `zone` that follow from its preset: brightness layer, lens flare, fog distance.
void fog_params(EnvZone& zone, const EnvInfo& info);
// out = the blend of a and b at weight (Q12, 0 = a). Fields the blend does not touch keep their value in `out`.
void blend_zone(const MapEnvParams& params, EnvZone& out, const EnvZone& a, const EnvZone& b, std::int32_t weight);
// Distance at which the fog is complete. `fog_enabled` and `dense_light` come from the world environment state.
std::int32_t fog_distance(const MapEnvParams& params, const EnvInfo& info, bool fog_enabled, bool dense_light);

// The same on the session's records. A state takes over the whole preset, bytes the blend does not name included, and
// only the named fields are written back.
EnvInfo env_info_of(const game::EnvironmentPreset& preset);
EnvZone env_zone_of(const game::EnvironmentState& state);
void store_env_zone(game::EnvironmentState& state, const EnvZone& zone);
// Installs `preset` in `state` and derives its values (fog_params).
void set_preset_state(game::EnvironmentState& state, const game::EnvironmentPreset& preset);
// out = the blend of a and b at weight (blend_zone).
void blend_state(const MapEnvParams& params, game::EnvironmentState& out, const game::EnvironmentState& a,
                 const game::EnvironmentState& b, std::int32_t weight);

}  // namespace sh::world
