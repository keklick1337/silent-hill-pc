// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The atmosphere of the world (`lighting` of the session, RENDER.md section 5.3): fog,
// world tint, screen brightness, the map light and the point light (the flashlight or a scripted lamp), with the
// operations the game runs on them: the fog ramp and its lookups, the darkness at a point, the colour blends of
// effects and the light in a model's frame.
//
// The engine owns the storage (detail::g_atmosphere_state). The game's environment record is a view of this storage
// with the same layout, so the field order and sizes of AtmosphereState are those of the record.
//
// Units: positions Q12 metres (+Y down), Q8 where noted; directions Q12; colours 8-bit per channel.

#include <array>
#include <cstdint>

#include "engine/world/world_env.h"

namespace sh::world {

struct Color8 {
    std::uint8_t r = 0, g = 0, b = 0;
    std::uint8_t code = 0;  // the packet code byte that travels with a colour; blends keep it
};
struct Vec3s16 {
    std::int16_t x = 0, y = 0, z = 0;
    std::int16_t pad = 0;
};
struct Vec3i32 {
    std::int32_t x = 0, y = 0, z = 0;
};
// Light colour matrix: one row per output channel, as the geometry unit takes it.
struct ColorMatrix {
    std::int16_t m[3][3] = {};
    std::int32_t t[3] = {};
};

struct AtmosphereFog {
    std::int32_t near_distance = 0;  // Q8
    std::int32_t far_distance = 0;   // Q8
    std::int32_t depth_shift = 0;    // 2^depth_shift is the depth at which the ramp ends
    std::int32_t intensity = 0;      // Q12 bias added to every fog amount (the area's fog density)
    Color8 color{};
};

// Light falloff along one axis, both signs of the offset: {x where it starts, value there, Q12 slope}.
using FalloffEntry = Vec3i32;

struct AtmosphereState {
    std::uint8_t light_mode = 0;          // 0 no directional light; 1, 2 lit (the map's preset)
    std::uint8_t fog_enabled = 0;
    std::uint8_t tint_light_overlap = 0;  // the screen tint overlay is on
    std::uint8_t light_level_max = 0;     // cap of the light level of lit vertices
    void* water_zones = nullptr;          // the map's water zones (opaque to the atmosphere)
    std::int32_t screen_brightness = 0;
    AtmosphereFog fog{};
    std::int32_t ambient = 0;             // Q12 map light
    Color8 char_light{};                  // flat light colour of characters
    Color8 world_tint{};
    ColorMatrix light_color{};            // colour of the directional light, per channel
    std::int32_t light_depth_cue = 0;     // depth cue slope the light installs (intensity >> 8)
    std::int16_t lens_flare_intensity = 0;
    std::int32_t light_intensity = 0;     // Q12
    Vec3s16 light_direction{};            // Q12 unit vector, world
    Vec3i32 light_position{};             // Q12, world
    Vec3s16 light_angles{};               // Q12 turns of light_direction
    Vec3s16 local_light_direction{};      // light direction in the frame of the model being drawn
    Vec3s16 local_light_position{};       // light position (Q8) in the frame of the model being drawn
    std::array<std::array<FalloffEntry, 2>, 3> falloff{};
    std::array<std::uint8_t, kFogRampSize> fog_ramp{};
    std::uint16_t clut_y = 0;             // CLUT row the character drawer gets for the model being drawn
};

// Result of the fog/tint blend of an effect colour.
struct FogTint {
    Color8 fog;   // fog colour scaled by what the fog lets through
    Color8 tint;  // world tint at the effect's light level, faded by the fog
};

// A model's frame: rotation (Q12) and translation (Q8), as the game keeps them.
struct ModelFrame {
    std::int16_t m[3][3] = {};
    std::int32_t t[3] = {};
};

class WorldAtmosphere {
public:
    WorldAtmosphere(AtmosphereState& state, const WorldEnvParams& params) : state_(&state), params_(&params) {}

    AtmosphereState& state() { return *state_; }
    const AtmosphereState& state() const { return *state_; }
    const WorldEnvParams& params() const { return *params_; }

    // Values at map start: no light, full ambient, neutral tint, white fog off, fog from 32 to 34 m.
    void reset();

    // ---- Fog ----
    void set_fog(bool enabled, std::uint8_t r, std::uint8_t g, std::uint8_t b);
    // Fog starts at `near_q12` and is complete at `far_q12`. Both are scaled by fog_distance_scale_pct().
    void set_fog_distance(std::int32_t near_q12, std::int32_t far_q12);
    // Runs set_fog_distance again with the last arguments (after the scale changed).
    void reapply_fog_distance();
    int fog_distance_scale_pct() const { return fog_scale_pct_; }
    void set_fog_distance_scale_pct(int pct) { fog_scale_pct_ = pct; }
    // The area's fog density: what the ramp leaves at `depth` becomes the bias of every fog amount.
    void set_fog_intensity(std::int32_t depth);
    // Fog amount 0..Q12(1) at a depth, interpolated between bins.
    std::int32_t fog_ramp_q12(std::int32_t depth) const;
    // Fog amount 0..255 at a depth, without interpolation.
    std::uint8_t fog_ramp_u8(std::int32_t depth) const;
    // Ramp byte at a view depth already in ramp units: 255 past the end of the ramp.
    std::int32_t fog_ramp_at(std::int32_t depth) const;
    // Depth past which the fog hides everything. With modern culling a thinner fog (`fog_strength` < 1) moves the
    // point where it is complete out by 1 / fog_strength (never below 0.1).
    std::int32_t fog_cull_distance(bool modern_culling, float fog_strength) const;
    // How much of an additive prim at view depth `z` survives the fog: 256 near, 0 where the fog is complete.
    int fog_keep(std::int32_t z) const;

    // ---- Map light ----
    // Light of the map preset: mode, ambient light, the light level cap, the tint (Q12 per channel) and the screen
    // brightness. The console overrides scale the light colour and the world tint.
    void set_lighting(std::uint8_t mode, std::int32_t ambient, std::uint8_t cap, const std::array<std::int32_t, 3>& tint,
                      std::int32_t brightness, const ColorScale& light_override, const ColorScale& world_override);
    // Falloff table of a light of `intensity` along `direction`.
    void set_light_falloff(std::int32_t intensity, const Vec3s16& direction);
    // Point light in world space. `angles` are those of `direction` (Q12 turns, z 0).
    void set_point_light(std::int32_t intensity, std::int16_t lens_flare, void* water_zones, const Vec3s16& direction,
                         const Vec3i32& position, const Vec3s16& angles);
    // Darkness 0..light_level_max at a world point (Q12).
    std::uint8_t darkness_at(std::int32_t x, std::int32_t y, std::int32_t z) const;

    // ---- Colour blends of effects ----
    // Fog and world tint for an effect at view depth `depth_q12` (Q12 of the ramp depth) and light level `level`.
    FogTint fog_tint(std::uint8_t level, std::int32_t depth_q12) const;
    // World tint darkened by `amount` (0..128); `code` is kept.
    Color8 faded_tint(std::uint8_t code, std::uint8_t amount) const;

    // ---- Light in a model's frame ----
    // Turns the point light into the frame of a model: direction (scaled by the light intensity) and position.
    void set_model_light(const ModelFrame& model);

    struct Fade {
        std::uint8_t amount = 0;
        // Rows the point was projected with (light direction, -normal, point relative to the light); the caller's
        // geometry unit keeps them loaded.
        std::int16_t rotation[3][3] = {};
    };
    // Light level of a ground point (Q8 in the model frame) with normal `normal`, for the fade of ground effects.
    // Runs set_model_light first.
    Fade fade_amount(const Vec3s16& point, const Vec3s16& normal, const ModelFrame& model);

private:
    FogRamp ramp() const;

    AtmosphereState* state_;
    const WorldEnvParams* params_;
    int fog_scale_pct_ = 100;
    std::int32_t last_fog_near_ = 0;
    std::int32_t last_fog_far_ = 0;
};

namespace detail {
// The one atmosphere state of the game (the game's record is bound to it, see above).
extern AtmosphereState g_atmosphere_state;
}  // namespace detail

// The game's atmosphere over detail::g_atmosphere_state, with the parameters of config/world_env.json.
WorldAtmosphere& atmosphere();

}  // namespace sh::world
