#include "engine/gameflow/flow_data.h"

#include <fstream>
#include <iterator>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::gameflow {

namespace {

std::string at(const json::Value& v, std::string_view what) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column()) + ": " + std::string(what);
}

// An optional sound or image name: absent or null = "", otherwise a non-empty string.
bool optional_name(const json::Value& item, std::string_view key, std::string& out, std::string& error) {
    const json::Value* v = item.find(key);
    if (v == nullptr || v->is_null()) {
        out.clear();
        return true;
    }
    if (!v->is_string() || v->string().empty()) {
        error = at(*v, "\"" + std::string(key) + "\" must be a non-empty name or null");
        return false;
    }
    out = v->string();
    return true;
}

bool optional_bool(const json::Value& item, std::string_view key, bool& out, std::string& error) {
    const json::Value* v = item.find(key);
    if (v == nullptr || v->is_null()) {
        out = false;
        return true;
    }
    if (!v->is_bool()) {
        error = at(*v, "\"" + std::string(key) + "\" must be true or false");
        return false;
    }
    out = v->boolean();
    return true;
}

}  // namespace

Result<std::string> read_config_file(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                     std::string_view name, std::string* source, std::vector<std::string>& warnings) {
    return read_data_file(gamedata_dir, mods_dir, "config", name, source, warnings);
}

Result<std::string> read_data_file(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                   std::string_view folder, std::string_view name, std::string* source,
                                   std::vector<std::string>& warnings) {
    namespace fs = std::filesystem;
    const bool gameplay = folder == "config";
    const fs::path relative = fs::path(std::string(folder)) / std::string(name);
    if (source != nullptr) {
        source->clear();
    }
    // Base + every active mod's patch, in load order.
    Result<mods::MergedFile> read = mods::read_data_file(gamedata_dir, mods_dir, relative, warnings);
    if (!read) {
        return Result<std::string>::fail(read.error().starts_with("no file")
                                             ? "cannot read " + (gamedata_dir / relative).string()
                                             : read.error());
    }
    for (const mods::DataLayer& mod : read.value().mods) {
        if (source != nullptr) {
            *source = mod.mod_id;
        }
        if (gameplay && !mod.gameplay) {
            warnings.push_back("mod " + mod.mod_id + " changes config/" + std::string(name) +
                               " but does not declare \"gameplay\": true in mod.json");
        }
    }
    return Result<std::string>::ok(std::move(read.value().text));
}

Result<DoorSounds> DoorSounds::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<DoorSounds>::fail("door_sounds.json: " + doc.error());
    }
    const json::Value* list = doc.value().find("sets");
    if (list == nullptr || !list->is_array() || list->array().empty()) {
        return Result<DoorSounds>::fail("door_sounds.json: \"sets\" must be a non-empty array");
    }
    DoorSounds out;
    std::string error;
    for (const json::Value& item : list->array()) {
        if (!item.is_object()) {
            return Result<DoorSounds>::fail("door_sounds.json: " + at(item, "a set must be an object"));
        }
        DoorSoundSet set;
        if (!optional_name(item, "open", set.open, error) || !optional_name(item, "close", set.close, error) ||
            !optional_bool(item, "starts_loading", set.starts_loading, error)) {
            return Result<DoorSounds>::fail("door_sounds.json: " + error);
        }
        out.sets_.push_back(std::move(set));
    }
    return Result<DoorSounds>::ok(std::move(out));
}

const DoorSoundSet& DoorSounds::set(std::size_t index) const {
    static const DoorSoundSet kNone{};
    return index < sets_.size() ? sets_[index] : kNone;
}

Result<GameOverTips> GameOverTips::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<GameOverTips>::fail("game_over_tips.json: " + doc.error());
    }
    const json::Value* list = doc.value().find("tips");
    if (list == nullptr || !list->is_array() || list->array().empty() || list->array().size() > kMaxTips) {
        return Result<GameOverTips>::fail("game_over_tips.json: \"tips\" must hold 1 to 16 tips");
    }
    GameOverTips out;
    std::string error;
    for (const json::Value& item : list->array()) {
        if (!item.is_object()) {
            return Result<GameOverTips>::fail("game_over_tips.json: " + at(item, "a tip must be an object"));
        }
        GameOverTip tip;
        if (!optional_name(item, "image", tip.image, error) || !optional_bool(item, "dark", tip.dark, error)) {
            return Result<GameOverTips>::fail("game_over_tips.json: " + error);
        }
        if (tip.image.empty()) {
            return Result<GameOverTips>::fail("game_over_tips.json: " + at(item, "a tip needs an \"image\""));
        }
        out.tips_.push_back(std::move(tip));
    }
    return Result<GameOverTips>::ok(std::move(out));
}

Result<TitleFogPalette> TitleFogPalette::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<TitleFogPalette>::fail("title_fog.json: " + doc.error());
    }
    const json::Value* list = doc.value().find("colors");
    if (list == nullptr || !list->is_array() || list->array().size() != kColors) {
        return Result<TitleFogPalette>::fail("title_fog.json: \"colors\" must hold 256 [r, g, b] entries");
    }
    TitleFogPalette out;
    for (const json::Value& item : list->array()) {
        bool ok = item.is_array() && item.array().size() == 3;
        std::uint8_t c[3] = {0, 0, 0};
        for (std::size_t k = 0; ok && k < 3; ++k) {
            const json::Value& v = item.array()[k];
            ok = v.is_integer() && v.integer() >= 0 && v.integer() <= 255;
            if (ok) {
                c[k] = static_cast<std::uint8_t>(v.integer());
            }
        }
        if (!ok) {
            return Result<TitleFogPalette>::fail("title_fog.json: " + at(item, "a colour must be [r, g, b] of 0..255"));
        }
        out.colors_.push_back(Rgb{c[0], c[1], c[2]});
    }
    return Result<TitleFogPalette>::ok(std::move(out));
}

void GameOverTips::restart_if_all_seen(std::uint16_t& seen) const {
    const std::uint32_t all = (1u << tips_.size()) - 1u;
    if (seen == all) {
        seen = 0;
    }
}

std::int32_t GameOverTips::total_weight(std::uint16_t seen, bool dark_scene) const {
    std::int32_t total = 0;
    for (std::size_t i = 0; i < tips_.size(); ++i) {
        if (!((seen >> i) & 1u)) {
            total += tips_[i].dark == dark_scene ? 3 : 1;
        }
    }
    return total;
}

std::size_t GameOverTips::pick(std::uint16_t seen, bool dark_scene, std::int32_t roll) const {
    std::size_t i = 0;
    for (; i < tips_.size(); ++i) {
        if ((seen >> i) & 1u) {
            continue;
        }
        if (tips_[i].dark == dark_scene) {
            if (roll < 3) {
                break;
            }
            roll -= 3;
        } else {
            if (roll <= 0) {
                break;
            }
            --roll;
        }
    }
    return i;
}

void GameOverTips::mark_seen(std::uint16_t& seen, std::size_t tip) {
    seen = static_cast<std::uint16_t>(seen | (1u << (tip & 0x1F)));
}

}  // namespace sh::gameflow
