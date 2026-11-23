// SPDX-License-Identifier: GPL-3.0-or-later
// The effect textures a map loads: which TIM pictures go to
// video memory when a map is entered (glass, water, fire, ...), where (one of the two shared image places and
// its row and palette row), and on which maps, some only while a story flag is clear. The data is
// gamedata/effects/effect_textures.json (a mod replaces the file whole); the game loads the files.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::effects {

// The shared image places of the effect pictures.
enum class EffectImage : std::uint8_t {
    Effects,  // most effect sheets
    Fire,     // the fire sheet's place
    Blood,    // the blood sheet loaded at boot
};

struct EffectTexture {
    std::string name;
    std::string file;                 // game file, "TIM/FIRE.TIM"
    EffectImage image = EffectImage::Effects;
    std::optional<std::int32_t> v;            // texel row the picture goes to; absent: the place's current one
    std::optional<std::int32_t> palette_row;  // its palette's row; absent: the current one
};

struct MapEffectTexture {
    std::string texture;
    std::string unless_flag;  // not loaded while this story flag is set (empty: always)
};

struct EffectTextureConfig {
    EffectTexture boot;                 // loaded once at boot (no map)
    std::vector<EffectTexture> textures;  // in load order
    struct Map {
        std::string map;
        std::vector<MapEffectTexture> textures;
    };
    std::vector<Map> maps;

    // The textures of `map` to load now, in load order; `flag_set` answers a story flag by name.
    std::vector<const EffectTexture*> for_map(std::string_view map,
                                              const std::function<bool(const std::string&)>& flag_set) const;
};

Result<EffectTextureConfig> parse_effect_textures(std::string_view json_text);
// gamedata/effects/effect_textures.json, or the last mod's copy.
Result<EffectTextureConfig> load_effect_textures(const std::filesystem::path& gamedata_dir,
                                                 const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::effects
