// The map light machine's numbers (src/engine/effects/map_lighting_config.h): the shipped file gives the original's
// preset pairs and the light rigs' Q12 constants, converted as the float literals were.

#include "engine/effects/map_lighting_config.h"

#include "framework/sh_test.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fx = sh::effects;

fx::MapLightingConfig shipped() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/effects/map_lighting.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    sh::Result<fx::MapLightingConfig> r = fx::parse_map_lighting(text);
    if (!r) std::fprintf(stderr, "map_lighting.json: %s\n", r.error().c_str());
    SH_CHECK(r);
    return r ? r.value() : fx::MapLightingConfig{};
}

bool pair_is(const fx::PresetPair& p, int a, int b) { return p[0] == a && p[1] == b; }

}  // namespace

SH_TEST(map_lighting_preset_pairs) {
    const fx::MapLightingConfig c = shipped();
    SH_CHECK(pair_is(c.presets_for(1, false), 1, 1));
    SH_CHECK(pair_is(c.presets_for(1, true), 2, 2));
    SH_CHECK(pair_is(c.presets_for(2, false), 6, 3));
    SH_CHECK(pair_is(c.presets_for(2, true), 7, 4));
    SH_CHECK(pair_is(c.presets_for(3, false), 9, 9));
    SH_CHECK(pair_is(c.presets_for(3, true), 9, 9));
    SH_CHECK(pair_is(c.presets_for(0, true), 1, 1));
    SH_CHECK(pair_is(c.presets_for(7, false), 1, 1));
    SH_CHECK(c.flash_preset == 8);
    SH_CHECK(c.flash_boost == 1228);  // Q12(0.3f)
}

SH_TEST(map_lighting_rigs) {
    const fx::MapLightingConfig c = shipped();
    // Q12(-0.08f), Q12(-0.28f), Q12(0.12f); Q12_ANGLE(-15.0f).
    SH_CHECK(c.flashlight.bone == 1 && c.flashlight.flare_bone == 0);
    SH_CHECK(c.flashlight.position[0] == -327 && c.flashlight.position[1] == -1146 && c.flashlight.position[2] == 491);
    SH_CHECK(c.flashlight.rotation[0] == -170 && c.flashlight.rotation[1] == 0 && c.flashlight.rotation[2] == 0);
    SH_CHECK(c.flashlight.intensity == 4096);
    // Q12(0.0f), Q12(-0.2f), Q12(-2.0f); Q12_ANGLE(10.0f).
    SH_CHECK(c.load_screen.bone == 0 && c.load_screen.flare_bone == 0);
    SH_CHECK(c.load_screen.position[0] == 0 && c.load_screen.position[1] == -819 && c.load_screen.position[2] == -8192);
    SH_CHECK(c.load_screen.rotation[0] == 113 && c.load_screen.intensity == 4096);
}

SH_TEST(map_lighting_rejects_bad_data) {
    SH_CHECK(fx::harry_bone_index("RightHand") == 10 && fx::harry_bone_index("Tail") == -1);
    const char* rig = R"({"bone": "Tail", "flare_bone": "Root", "position": [0, 0, 0], "rotation": [0, 0, 0],
                         "intensity": 1})";
    const std::string bad = std::string(R"({"light_modes": [], "default_presets": [1, 1], "flash_preset": 8,
        "flash_boost": 0, "light_rigs": {"flashlight": )") + rig + R"(, "load_screen": )" + rig + "}}";
    SH_CHECK(!fx::parse_map_lighting(bad));
    SH_CHECK(!fx::parse_map_lighting(R"({"light_modes": [{"mode": 1, "presets": [1]}]})"));
}
