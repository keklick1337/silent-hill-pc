// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// World environment numbers (docs/RENDER.md section 5.3): the fog depth ramp and its lookups, the table of
// directional light falloff, the darkness at a point and the light/tint set-up of a map. Pure integer math on engine
// structs; the game keeps its own state record and calls these through its world-environment bridge.

#include <array>
#include <cstdint>
#include <string_view>

#include "formats/common/status.h"

namespace sh::world {

constexpr int kFogRampSize = 128;

// Numbers of the environment, from gamedata/config/world_env.json (a mod replaces the file whole).
struct WorldEnvParams {
    // Fog depth curve: 16 bins over the first half of the depth range, linear between neighbours (0..255).
    std::array<std::int32_t, 17> fog_curve{0,   0,   0x26, 0x4C, 0x5E, 0x70, 0x85, 0x99, 0xB0,
                                           0xBD, 0xD1, 0xDC, 0xE6, 0xF0, 0xF5, 0xFA, 0xFF};
    // Directional light: the weight of a light direction axis is (axis + bias) / span, in Q12.
    std::int32_t light_axis_bias = 5324;
    std::int32_t light_axis_span = 9420;
    // Reference light sample (Q12 x, Q8-ish y, Q12 z) the per-axis falloff table is scaled from.
    std::array<std::int32_t, 3> light_sample{0, 204, -311};

    static Result<WorldEnvParams> parse(std::string_view json);
};

// Fog state derived from the distances (all Q8 metres).
struct FogRamp {
    std::int32_t near_distance = 0;
    std::int32_t far_distance = 0;
    std::int32_t depth_shift = 0;  // 2^depth_shift is the depth at which the ramp ends
    std::array<std::uint8_t, kFogRampSize> ramp{};
};

// Builds the ramp for fog that starts at `near_q8` (a distance of 0 or less is taken as 1) and is complete at far_q8.
FogRamp fog_ramp_build(const WorldEnvParams& params, std::int32_t near_q8, std::int32_t far_q8);
// Fog amount 0..Q12(1) at a depth (the depth in the same unit the game feeds the ramp), interpolated between bins.
std::int32_t fog_ramp_q12(const FogRamp& fog, std::int32_t depth);
// Fog amount 0..255 at a depth, without interpolation.
std::uint8_t fog_ramp_u8(const FogRamp& fog, std::int32_t depth);

// Falloff of a directional light along one axis, for both signs of the axis offset.
struct LightFalloff {
    std::int32_t x = 0;       // where the falloff starts
    std::int32_t base = 0;    // value there
    std::int32_t slope = 0;   // Q12 slope past it
};
using LightFalloffTable = std::array<std::array<LightFalloff, 2>, 3>;  // [axis][0 positive, 1 negative side]

// Table for a light of `intensity` pointing along `dir` (Q12 components).
LightFalloffTable light_falloff_compute(const WorldEnvParams& params, std::int32_t intensity,
                                        const std::array<std::int32_t, 3>& dir);

// Darkness 0..cap of a point whose offset from the light is `offset_q8`. `lit_mode` 0 means no directional light.
std::uint8_t darkness_at(const LightFalloffTable& table, bool lit_mode, std::int32_t ambient_q12_5,
                         std::int32_t cap, const std::array<std::int32_t, 3>& offset_q8);

// Colour override from the console: the value is scaled by channel / 255.
struct ColorScale {
    bool active = false;
    std::uint8_t r = 255, g = 255, b = 255;
};

struct LightSetup {
    std::uint8_t mode = 0;
    std::int32_t ambient = 0;           // map lighting, kept in the units it came in
    std::uint8_t cap = 0;               // darkness cap
    std::int32_t brightness = 0;
    std::array<std::uint8_t, 3> world_tint{};   // flat ambient tint, 8-bit
    std::array<std::int16_t, 3> light_color{};  // colour of the directional light, Q12 per channel
    std::array<std::uint8_t, 3> char_light{};   // colour lighting of characters
};

// Light and tint of a map from the values of its preset. `tint` is the Q12 colour, `ambient` the light amount.
LightSetup light_setup(std::uint8_t mode, std::int32_t ambient, std::uint8_t cap, const std::array<std::int32_t, 3>& tint,
                       std::int32_t brightness, const ColorScale& light_override, const ColorScale& world_override);

}  // namespace sh::world
