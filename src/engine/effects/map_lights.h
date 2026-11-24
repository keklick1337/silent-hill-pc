// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The lights of a map for the modern lighting (docs/RENDER.md section 10): gamedata/maps/<map>/lights.json,
// patched by mods light by light ("name"). Map lights (lamps, fires, glows), lights that follow a bone of Harry while
// an effect shows (the match in his hand), and for daytime maps a sun and a sky. The original image has none of
// them: only the setting rt_lights uses them, and a map without the file has none.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formats/common/status.h"

namespace sh::effects {

struct MapLight {
    std::string name;
    bool spot = false;
    std::array<float, 3> position{0, 0, 0};   // metres, PS1 axes (y down); with a bone: the offset in the bone
    std::array<float, 3> direction{0, 1, 0};  // spot: the axis (world, or the bone's space)
    float cone_inner = 30.0f;                 // spot: degrees from the axis, full light inside, none past outer
    float cone_outer = 45.0f;
    std::array<float, 3> color{1, 1, 1};  // linear
    float intensity = 1.0f;
    float range = 6.0f;    // metres: the light is gone here
    float radius = 0.05f;  // metres: the emitter's size (shadow softness)
    bool casts_shadows = true;
    // Without ray tracing its shadows come from the flashlight's shadow map while the flashlight is off (one light
    // at a time; the shadows setting): a held flame, whose shadow is the one the eye looks for.
    bool shadow_map = false;
    bool gi = true;
    std::vector<std::string> flags_set;    // every one set (event flag names; "when": "flag:<name>")
    std::vector<std::string> flags_clear;  // every one clear ("when": "!flag:<name>")
    // A lamp placed by hand where the original only draws the fixture: shown only with the setting extra_lights.
    bool extra = false;
    // Follows a bone of Harry ("RightHand"; the only skeleton the frame knows by name). -1: placed in the world.
    int bone = -1;
    // Shines only on frames where this effect is shown ("HandFlame"); empty: always.
    std::string while_effect;
    // 0..1: how much of the intensity follows the effect's own brightness (the flame's flicker).
    float flicker = 0.0f;
};

inline constexpr std::size_t kMaxMapCeilings = 8;

// A roof the cells do not draw (their rooms end at the walls' tops): it keeps the sun and the sky out of the rooms
// below it, so they reach indoors only through real openings. A horizontal rectangle, metres, PS1 axes.
struct MapCeiling {
    std::array<float, 4> rect{};  // x0, z0, x1, z1
    float height = 0.0f;          // y (down is positive: a roof above the floor is negative)
};

struct MapSun {
    std::array<float, 3> direction{0, 1, 0};  // the way the light travels, PS1 axes (y down)
    std::array<float, 3> color{1, 1, 1};
    float intensity = 0.0f;
    float angular_radius = 0.5f;  // degrees: the softness of its shadows
    std::array<float, 3> sky_color{0, 0, 0};
    float sky_intensity = 0.0f;
    std::vector<MapCeiling> ceilings;
    std::vector<std::string> flags_set;
    std::vector<std::string> flags_clear;
};

// A light at each lamp glow the original draws (the halo sprites of the cells' lamps): the default for every map is
// gamedata/effects/lamp_lights.json, a map's lights.json may change it ("glows") or switch it off ("glows": null).
struct GlowLights {
    bool on = false;
    std::array<float, 3> color{1.0f, 0.85f, 0.6f};
    float intensity = 0.6f;
    float range = 6.0f;    // metres
    float radius = 0.08f;  // metres
    float drop = 0.0f;     // metres below the glow (y down: positive is lower), where the bulb shines from
    bool casts_shadows = true;
    bool gi = true;
};

struct MapLights {
    std::vector<MapLight> lights;
    std::optional<MapSun> sun;
    // Unset: the default glow lights; set: this map's own (on = false: none).
    std::optional<GlowLights> glows;
};

// gamedata/effects/lamp_lights.json: the glow lights of maps whose lights.json does not say.
Result<GlowLights> parse_lamp_lights(std::string_view json_text);
Result<GlowLights> load_lamp_lights(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                    std::string* source);

// The effects a light can wait for (MapLight::while_effect).
inline constexpr std::string_view kLightEffects[] = {"HandFlame"};

// The effects a light can follow report themselves while they are drawn, with their brightness 0..1; the lights
// read them when the frame is described and then clear them (render only, never the simulation).
void effect_shown(std::string_view effect, float brightness);
std::optional<float> effect_brightness(std::string_view effect);
void clear_effects_shown();

// A light an effect gives only while it is drawn (a gun's muzzle flash, weapons/<Name>.json "muzzle_light"): reported
// every frame the effect shows, under a key so a frame drawn twice gives it once, and cleared with the effects above.
struct EffectLight {
    std::array<float, 3> position{0, 0, 0};  // world units (PS1 axes, 256 per metre)
    std::array<float, 3> color{1, 1, 1};     // linear, times the intensity and the effect's brightness
    float range = 4.0f;                      // metres
    float radius = 0.05f;                    // metres
    float near_clip = 0.0f;                  // metres: shadow rays stop this short of it (the gun that fires it)
    bool casts_shadows = true;
    bool gi = true;
};
void effect_light_shown(int key, const EffectLight& light);
const std::vector<std::pair<int, EffectLight>>& effect_lights_shown();

// The light of a fire effect (gamedata/effects/fire_lights.json "fires"): every flame particle the effect draws
// reports its point and its share of light, and the light sits at their weighted middle with an intensity that
// follows their sum, so it grows, flickers and dies out with the fire itself.
struct FireLight {
    std::array<float, 3> color{1.0f, 0.6f, 0.3f};  // linear
    float intensity = 2.0f;                        // at "full" (the light grows past it up to twice)
    float full = 32.0f;                            // the particles' summed share at which the intensity is reached
    float range = 8.0f;                            // metres
    float radius = 0.3f;                           // metres
    float lift = 0.0f;                             // metres above the particles' middle (y down: positive is lower)
    float flicker = 0.0f;                          // 0..1: share of the intensity a fast render-side noise takes
    bool casts_shadows = true;
    bool gi = true;
};

// The fire effects that report their particles (FireLight); a name of fire_lights.json must be one of them.
inline constexpr std::string_view kFireEffects[] = {"ParticleEmitter", "FirePool"};

using FireLights = std::vector<std::pair<std::string, FireLight>>;
Result<FireLights> parse_fire_lights(std::string_view json_text);
Result<FireLights> load_fire_lights(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                    std::string* source);

// A drawn flame particle of a fire effect: its point in world units (PS1 axes, 256 per metre) and its share of light
// (1: one fully bright particle). Render only: the simulation never reads it.
void fire_particle_shown(std::string_view fire, const std::array<float, 3>& position, float amount);
// The particles a fire reported since the last call (sum of the shares, weighted middle); clears them.
struct FireSample {
    float amount = 0.0f;
    std::array<float, 3> position{0, 0, 0};
};
FireSample take_fire_particles(std::string_view fire);

// The current map's lights into the frame (scene::Frame lights and sun): run once per frame, before the draw.
void describe_map_lights();

Result<MapLights> parse_map_lights(std::string_view json_text);
// gamedata/maps/<map>/lights.json with the mods' patches; an empty set when no file exists.
Result<MapLights> load_map_lights(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                  std::string_view map, std::string* source);

}  // namespace sh::effects
