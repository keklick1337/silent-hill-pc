// SPDX-License-Identifier: GPL-3.0-or-later
// The numbers of the map light machine: which pair of environment presets
// (gamedata/config/map_info.json "environments") a map starts with for its light mode, the preset a flash of light
// switches to, and the light rigs the flashlight and the load screen put on Harry. The data is
// gamedata/effects/map_lighting.json (a mod replaces the file whole).

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::effects {

// Two presets: flashlight off, flashlight on (indices into the environment list).
using PresetPair = std::array<std::int32_t, 2>;

struct LightModePresets {
    std::int32_t mode = 0;
    PresetPair open{};   // the map has no active chunk
    PresetPair chunk{};  // an interior with one or two active chunks
};

// Where the light sits on Harry: the bone it follows (Harry's bone index), the bone the lens flare follows, the
// offset in the bone (Q12 world units), the direction (Q12 angles) and the intensity (Q12).
struct LightRig {
    std::int32_t bone = 0;
    std::int32_t flare_bone = 0;
    std::array<std::int32_t, 3> position{};
    std::array<std::int16_t, 3> rotation{};
    std::int16_t intensity = 4096;
};

struct MapLightingConfig {
    std::vector<LightModePresets> light_modes;
    PresetPair default_presets{};
    std::int32_t flash_preset = 0;      // a flash of light (bit 0 of the current preset's flags) jumps to it
    std::int16_t flash_boost = 0;       // otherwise (bit 1) the spot light grows by this much (Q12)
    LightRig flashlight;
    LightRig load_screen;

    // The pair of a light mode; the default pair for an unknown mode.
    PresetPair presets_for(std::int32_t mode, bool active_chunk) const;
};

// Harry's bone index by name ("Root", "Torso", ... "RightFoot"); -1 for an unknown name.
int harry_bone_index(std::string_view name);

Result<MapLightingConfig> parse_map_lighting(std::string_view json_text);
// gamedata/effects/map_lighting.json, or the last mod's copy.
Result<MapLightingConfig> load_map_lighting(const std::filesystem::path& gamedata_dir,
                                            const std::filesystem::path& mods_dir, std::string* source);

}  // namespace sh::effects
