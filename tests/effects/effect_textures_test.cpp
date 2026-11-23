// Map effect textures (src/engine/effects/effect_textures.h): the shipped file, the load order and the story flag
// that turns a texture off.

#include "engine/effects/effect_textures.h"

#include "framework/sh_test.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fx = sh::effects;

fx::EffectTextureConfig shipped() {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/effects/effect_textures.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    sh::Result<fx::EffectTextureConfig> r = fx::parse_effect_textures(text);
    if (!r) std::fprintf(stderr, "effect_textures.json: %s\n", r.error().c_str());
    SH_CHECK(r);
    return r ? r.value() : fx::EffectTextureConfig{};
}

}  // namespace

SH_TEST(effect_textures_shipped_file) {
    const fx::EffectTextureConfig c = shipped();
    SH_CHECK(c.boot.file == "TIM/BLD.TIM" && c.boot.image == fx::EffectImage::Blood && !c.boot.v);
    SH_CHECK(c.textures.size() == 7);
    // The original's flag order is the load order.
    SH_CHECK(c.textures[0].name == "glass" && c.textures[3].name == "fire" && c.textures[4].name == "flesh");
    SH_CHECK(c.textures[3].image == fx::EffectImage::Fire && c.textures[3].v == 120 && !c.textures[3].palette_row);
    SH_CHECK(c.textures[4].v == 64 && c.textures[4].palette_row == 4);
}

SH_TEST(effect_textures_for_map) {
    const fx::EffectTextureConfig c = shipped();
    const auto none = [](const std::string&) { return false; };
    const auto all = [](const std::string&) { return true; };
    // Listed "fire", "flesh": loaded fire first.
    const auto m15 = c.for_map("map1_s05", none);
    SH_CHECK(m15.size() == 2 && m15[0]->name == "fire" && m15[1]->name == "flesh");
    // The vines burnt: no fire on map3_s05.
    SH_CHECK(c.for_map("map3_s05", none).size() == 1 && c.for_map("map3_s05", all).empty());
    SH_CHECK(c.for_map("map0_s01", [](const std::string& f) { return f == "M0S01_AirScreamerFightStarted"; }).empty());
    SH_CHECK(c.for_map("map0_s00", none).empty());
}

SH_TEST(effect_textures_rejects_unknown_names) {
    const char* bad = R"({"boot": {"file": "A", "image": "blood"}, "textures": [], "maps": {"m": ["nope"]}})";
    SH_CHECK(!fx::parse_effect_textures(bad));
    const char* image = R"({"boot": {"file": "A", "image": "sky"}, "textures": [], "maps": {}})";
    SH_CHECK(!fx::parse_effect_textures(image));
}
