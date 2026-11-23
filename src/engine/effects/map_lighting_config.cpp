// SPDX-License-Identifier: GPL-3.0-or-later
// The numbers of the map light machine (map_lighting_config.h).

#include "engine/effects/map_lighting_config.h"

#include <stdexcept>

#include "engine/effects/effects_file.h"
#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::effects {

namespace {

constexpr std::string_view kHarryBones[] = {
    "Root",          "Torso",        "Head",         "LeftShoulder", "LeftUpperArm", "LeftForearm",
    "LeftHand",      "RightShoulder", "RightUpperArm", "RightForearm", "RightHand",    "Hips",
    "LeftThigh",     "LeftShin",     "LeftFoot",     "RightThigh",   "RightShin",    "RightFoot",
};

// The data gives metres, degrees and plain factors; the game's constants were float literals truncated to Q12
// in single precision, so the conversion is done the same way.
std::int32_t q12_of(double value) {
    const float f = static_cast<float>(value);
    return static_cast<std::int32_t>(f * 4096.0f);
}

std::int16_t q12_angle_of(double degrees) {
    const float turn = static_cast<float>(degrees) / 360.0f;
    return static_cast<std::int16_t>(static_cast<std::int32_t>(turn * 4096.0f));
}

double number_of(const json::Value& v) {
    if (!v.is_number()) parse::fail(v, "a number is expected");
    return v.number();
}

PresetPair pair_of(const json::Value& v) {
    if (!v.is_array() || v.array().size() != 2) parse::fail(v, "a preset pair is [flashlight off, flashlight on]");
    return {parse::integer(v.array()[0], 0, 255), parse::integer(v.array()[1], 0, 255)};
}

std::int32_t bone_of(const json::Value& parent, std::string_view key) {
    const json::Value& v = parse::member(parent, key);
    if (!v.is_string()) parse::fail(v, "a bone is one of Harry's bone names");
    const int index = harry_bone_index(v.string());
    if (index < 0) parse::fail(v, "unknown bone \"" + v.string() + "\"");
    return index;
}

LightRig rig_of(const json::Value& v) {
    if (!v.is_object()) parse::fail(v, "a light rig is an object");
    parse::reject_unknown(v, {"bone", "flare_bone", "position", "rotation", "intensity"});
    LightRig rig;
    rig.bone = bone_of(v, "bone");
    rig.flare_bone = bone_of(v, "flare_bone");
    const json::Value& position = parse::member(v, "position");
    if (!position.is_array() || position.array().size() != 3) parse::fail(position, "\"position\" is [x, y, z] in metres");
    for (std::size_t i = 0; i < 3; ++i) rig.position[i] = q12_of(number_of(position.array()[i]));
    const json::Value& rotation = parse::member(v, "rotation");
    if (!rotation.is_array() || rotation.array().size() != 3) parse::fail(rotation, "\"rotation\" is [x, y, z] in degrees");
    for (std::size_t i = 0; i < 3; ++i) rig.rotation[i] = q12_angle_of(number_of(rotation.array()[i]));
    rig.intensity = static_cast<std::int16_t>(q12_of(number_of(parse::member(v, "intensity"))));
    return rig;
}

}  // namespace

int harry_bone_index(std::string_view name) {
    for (std::size_t i = 0; i < std::size(kHarryBones); ++i) {
        if (kHarryBones[i] == name) return static_cast<int>(i);
    }
    return -1;
}

PresetPair MapLightingConfig::presets_for(std::int32_t mode, bool active_chunk) const {
    for (const LightModePresets& m : light_modes) {
        if (m.mode == mode) return active_chunk ? m.chunk : m.open;
    }
    return default_presets;
}

Result<MapLightingConfig> parse_map_lighting(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) return Result<MapLightingConfig>::fail(doc.error());
    const json::Value& root = doc.value();
    if (!root.is_object()) throw std::runtime_error("the file must be an object");
    parse::reject_unknown(root, {"light_modes", "default_presets", "flash_preset", "flash_boost", "light_rigs"});

    MapLightingConfig cfg;
    const json::Value& modes = parse::member(root, "light_modes");
    if (!modes.is_array()) parse::fail(modes, "\"light_modes\" is a list");
    for (const json::Value& m : modes.array()) {
        if (!m.is_object()) parse::fail(m, "a light mode is an object");
        parse::reject_unknown(m, {"mode", "presets", "chunk_presets"});
        LightModePresets p;
        p.mode = parse::integer_of(m, "mode", 0, 255);
        p.open = pair_of(parse::member(m, "presets"));
        p.chunk = m.find("chunk_presets") != nullptr ? pair_of(parse::member(m, "chunk_presets")) : p.open;
        for (const LightModePresets& other : cfg.light_modes) {
            if (other.mode == p.mode) parse::fail(m, "a light mode is listed twice");
        }
        cfg.light_modes.push_back(p);
    }
    cfg.default_presets = pair_of(parse::member(root, "default_presets"));
    cfg.flash_preset = parse::integer_of(root, "flash_preset", 0, 255);
    cfg.flash_boost = static_cast<std::int16_t>(q12_of(number_of(parse::member(root, "flash_boost"))));
    const json::Value& rigs = parse::member(root, "light_rigs");
    if (!rigs.is_object()) parse::fail(rigs, "\"light_rigs\" is an object");
    parse::reject_unknown(rigs, {"flashlight", "load_screen"});
    cfg.flashlight = rig_of(parse::member(rigs, "flashlight"));
    cfg.load_screen = rig_of(parse::member(rigs, "load_screen"));
    return Result<MapLightingConfig>::ok(std::move(cfg));
} catch (const std::exception& e) {
    return Result<MapLightingConfig>::fail(e.what());
}

Result<MapLightingConfig> load_map_lighting(const std::filesystem::path& gamedata_dir,
                                            const std::filesystem::path& mods_dir, std::string* source) {
    const std::filesystem::path relative = std::filesystem::path("effects") / "map_lighting.json";
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) *source = where;
    if (!read) return Result<MapLightingConfig>::fail(read.error());
    Result<MapLightingConfig> parsed = parse_map_lighting(read.value());
    if (!parsed) return Result<MapLightingConfig>::fail(where + ": " + parsed.error());
    return parsed;
}

}  // namespace sh::effects
