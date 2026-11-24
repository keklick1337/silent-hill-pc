// The map lights of the modern lighting (src/engine/effects/map_lights.h): every shipped
// gamedata/maps/*/lights.json parses, the keys take their units, and bad lights are rejected.

#include "engine/effects/map_lights.h"

#include "framework/sh_test.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fx = sh::effects;

SH_TEST(map_lights_shipped_files_parse) {
    int files = 0;
    for (const auto& map : std::filesystem::directory_iterator(std::string(SH_PROJECT_ROOT) + "/gamedata/maps")) {
        const std::filesystem::path p = map.path() / "lights.json";
        if (!std::filesystem::is_regular_file(p)) continue;
        std::ifstream in(p, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const sh::Result<fx::MapLights> r = fx::parse_map_lights(text);
        if (!r) std::fprintf(stderr, "%s: %s\n", p.string().c_str(), r.error().c_str());
        SH_CHECK(static_cast<bool>(r));
        ++files;
    }
    SH_CHECK(files > 0);
}

SH_TEST(map_lights_keys) {
    const sh::Result<fx::MapLights> r = fx::parse_map_lights(R"({
        "lights": [
            {"name": "Lamp", "position": [1, -2, 3], "color": [1, 0.5, 0.25], "intensity": 2, "range": 4},
            {"name": "Match", "bone": "RightHand", "position": [0, 0, 0.1], "while_effect": "HandFlame",
             "flicker": 0.5, "casts_shadows": false, "shadow_map": true},
            {"name": "Beam", "type": "spot", "position": [0, 0, 0], "direction": [0, 2, 0], "cone": [20, 40]}
        ],
        "sun": {"direction": [1, 1, 0], "intensity": 0.5},
        "sky": {"color": [0.5, 0.5, 0.5], "intensity": 0.2}
    })");
    SH_CHECK(static_cast<bool>(r));
    if (!r) return;
    const fx::MapLights& m = r.value();
    SH_CHECK(m.lights.size() == 3);
    SH_CHECK(m.lights[0].position[1] == -2.0f && m.lights[0].range == 4.0f && m.lights[0].bone < 0);
    SH_CHECK(m.lights[1].bone >= 0 && m.lights[1].while_effect == "HandFlame" && !m.lights[1].casts_shadows);
    SH_CHECK(m.lights[1].shadow_map && !m.lights[0].shadow_map);
    SH_CHECK(m.lights[2].spot && m.lights[2].direction[1] == 1.0f && m.lights[2].cone_outer == 40.0f);
    SH_CHECK(m.sun.has_value() && m.sun->intensity == 0.5f && m.sun->sky_intensity == 0.2f);
    SH_CHECK(m.sun && m.sun->direction[0] > 0.70f && m.sun->direction[0] < 0.71f);
}

SH_TEST(map_lights_bad_lights_are_rejected) {
    SH_CHECK(!fx::parse_map_lights(R"({"lights": [{"position": [0, 0, 0]}]})"));
    SH_CHECK(!fx::parse_map_lights(R"({"lights": [{"name": "A", "position": [0, 0, 0], "bone": "Tail"}]})"));
    SH_CHECK(!fx::parse_map_lights(R"({"lights": [{"name": "A", "position": [0, 0, 0], "while_effect": "X"}]})"));
    SH_CHECK(!fx::parse_map_lights(R"({"lights": [{"name": "A", "position": [0, 0, 0], "color": [2, 0, 0]}]})"));
    SH_CHECK(!fx::parse_map_lights(
        R"({"lights": [{"name": "A", "position": [0, 0, 0]}, {"name": "A", "position": [1, 0, 0]}]})"));
    SH_CHECK(!fx::parse_map_lights(R"({"sun": {"direction": [0, 0, 0]}})"));
}

SH_TEST(map_lights_when_and_extra) {
    auto r = fx::parse_map_lights(R"({
        "lights": [
            {"name": "Ceiling", "extra": true, "position": [0, -3, 0], "when": "flag:M3S01_GeneratorOn"},
            {"name": "Tube", "position": [0, -3, 0], "when": ["!flag:M7S02_GeneratorStopped", "flag:A"],
             "flags_set": ["B"]}
        ],
        "sun": {"direction": [0, 1, 0], "when": "!flag:M0S00_AlleyNearlyDark"}
    })");
    SH_CHECK(static_cast<bool>(r));
    if (!r) return;
    const fx::MapLights& m = r.value();
    SH_CHECK(m.lights[0].extra && !m.lights[1].extra);
    SH_CHECK(m.lights[0].flags_set.size() == 1 && m.lights[0].flags_set[0] == "M3S01_GeneratorOn");
    SH_CHECK(m.lights[0].flags_clear.empty());
    SH_CHECK(m.lights[1].flags_set.size() == 2 && m.lights[1].flags_set[0] == "B" && m.lights[1].flags_set[1] == "A");
    SH_CHECK(m.lights[1].flags_clear.size() == 1 && m.lights[1].flags_clear[0] == "M7S02_GeneratorStopped");
    SH_CHECK(m.sun && m.sun->flags_clear.size() == 1 && m.sun->flags_set.empty());
    SH_CHECK(!fx::parse_map_lights(R"({"lights": [{"name": "A", "position": [0, 0, 0], "when": "A"}]})"));
    SH_CHECK(!fx::parse_map_lights(R"({"lights": [{"name": "A", "position": [0, 0, 0], "when": "flag:"}]})"));
    SH_CHECK(!fx::parse_map_lights(R"({"lights": [{"name": "A", "position": [0, 0, 0], "when": [1]}]})"));
    SH_CHECK(!fx::parse_map_lights(R"({"lights": [{"name": "A", "position": [0, 0, 0], "extra": 1}]})"));
}

SH_TEST(map_lights_fire_lights_parse) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/effects/fire_lights.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const sh::Result<fx::FireLights> r = fx::parse_fire_lights(text);
    if (!r) std::fprintf(stderr, "fire_lights.json: %s\n", r.error().c_str());
    SH_CHECK(static_cast<bool>(r) && r.value().size() == 2);
    SH_CHECK(!fx::parse_fire_lights(R"({"fires": {"Torch": {}}})"));
    SH_CHECK(!fx::parse_fire_lights(R"({"fires": {"FirePool": {"full": 0}}})"));
    fx::fire_particle_shown("FirePool", {0, 0, 0}, 1.0f);
    fx::fire_particle_shown("FirePool", {256, 0, 0}, 3.0f);
    const fx::FireSample s = fx::take_fire_particles("FirePool");
    SH_CHECK(s.amount == 4.0f && s.position[0] == 192.0f);
    SH_CHECK(fx::take_fire_particles("FirePool").amount == 0.0f);
}

SH_TEST(map_lights_sun_ceilings) {
    auto r = fx::parse_map_lights(
        R"({"sun": {"direction": [0, 1, 0], "ceilings": [{"rect": [-10, -5, 10, 5], "height": -4.5}]}})");
    SH_CHECK(static_cast<bool>(r));
    if (!r) return;
    const fx::MapLights& m = r.value();
    SH_CHECK(m.sun && m.sun->ceilings.size() == 1);
    SH_CHECK(m.sun && m.sun->ceilings[0].rect[2] == 10.0f && m.sun->ceilings[0].height == -4.5f);
    SH_CHECK(!fx::parse_map_lights(R"({"sun": {"direction": [0, 1, 0], "ceilings": [{"rect": [10, 0, 0, 5], "height": 0}]}})"));
    SH_CHECK(!fx::parse_map_lights(R"({"sun": {"direction": [0, 1, 0], "ceilings": [{"rect": [0, 0, 1, 1]}]}})"));
}

SH_TEST(map_lights_lamp_glow_defaults_parse) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/effects/lamp_lights.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const sh::Result<fx::GlowLights> r = fx::parse_lamp_lights(text);
    SH_CHECK(static_cast<bool>(r) && r.value().on && r.value().range > 0.0f);
    const sh::Result<fx::MapLights> off = fx::parse_map_lights(R"({"glows": null})");
    SH_CHECK(static_cast<bool>(off) && off.value().glows.has_value() && !off.value().glows->on);
}
