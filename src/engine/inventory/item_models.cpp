// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/item_models.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::inventory {
namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

void reject_unknown(const json::Value& parent, std::string_view where, std::initializer_list<std::string_view> known) {
    if (!parent.is_object()) {
        fail(std::string(where) + ": expected an object");
    }
    for (const auto& [key, value] : parent.members()) {
        if (std::find(known.begin(), known.end(), key) == known.end()) {
            fail(std::string(where) + ": unknown key \"" + key + "\"");
        }
    }
}

std::string opt_string(const json::Value& parent, std::string_view key, std::string_view where) {
    const json::Value* v = parent.find(key);
    if (v == nullptr) {
        return {};
    }
    if (!v->is_string()) {
        fail(std::string(where) + ": \"" + std::string(key) + "\" must be a string");
    }
    return v->string();
}

ItemModelFiles parse_files(std::string_view text) {
    Result<json::Value> doc = json::parse(text);
    if (!doc) {
        fail(doc.error());
    }
    const json::Value& root = doc.value();
    reject_unknown(root, "item_models",
                   {"close_up_fallback", "common_texture", "model_texture", "key_textures", "maps", "results_items"});
    ItemModelFiles files;
    files.close_up_fallback = opt_string(root, "close_up_fallback", "item_models");
    files.common_texture = opt_string(root, "common_texture", "item_models");
    files.model_texture = opt_string(root, "model_texture", "item_models");
    if (const json::Value* awards = root.find("results_items"); awards != nullptr) {
        if (!awards->is_array()) {
            fail("\"results_items\" must be an array");
        }
        for (const json::Value& a : awards->array()) {
            if (!a.is_string() || a.string().empty()) {
                fail("\"results_items\" holds item names");
            }
            files.results_items.push_back(a.string());
        }
        if (files.results_items.size() > kResultsItems) {
            fail("\"results_items\" holds at most 6 items (the results screen's models)");
        }
    }
    if (const json::Value* keys = root.find("key_textures"); keys != nullptr) {
        if (!keys->is_array()) {
            fail("\"key_textures\" must be an array");
        }
        for (const json::Value& k : keys->array()) {
            if (!k.is_string() || k.string().empty()) {
                fail("\"key_textures\" holds file names");
            }
            files.key_textures.push_back(k.string());
        }
        if (files.key_textures.size() > kMaxKeyTextures) {
            fail("\"key_textures\" holds at most 6 sheets (one residency bit each)");
        }
    }
    if (const json::Value* maps = root.find("maps"); maps != nullptr) {
        if (!maps->is_object()) {
            fail("\"maps\" must be an object");
        }
        for (const auto& [name, entry] : maps->members()) {
            const std::string where = "maps." + name;
            reject_unknown(entry, where, {"key_texture", "models"});
            MapItemPack pack;
            pack.key_texture = opt_string(entry, "key_texture", where);
            pack.models = opt_string(entry, "models", where);
            if (!pack.key_texture.empty() &&
                std::find(files.key_textures.begin(), files.key_textures.end(), pack.key_texture) ==
                    files.key_textures.end()) {
                fail(where + ": key texture " + pack.key_texture + " is not in \"key_textures\"");
            }
            files.maps.insert_or_assign(name, std::move(pack));
        }
    }
    return files;
}

}  // namespace

Result<ItemModelFiles> parse_item_models(std::string_view json_text) try {
    return Result<ItemModelFiles>::ok(parse_files(json_text));
} catch (const std::exception& e) {
    return Result<ItemModelFiles>::fail(e.what());
}

Result<ItemModelFiles> load_item_models(const std::filesystem::path& gamedata_dir,
                                        const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "item_models.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<ItemModelFiles>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<ItemModelFiles> parsed = parse_item_models(text);
    if (!parsed) {
        return Result<ItemModelFiles>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

const MapItemPack* map_pack(const ItemModelFiles& files, std::string_view map) {
    const auto it = files.maps.find(map);
    return it != files.maps.end() ? &it->second : nullptr;
}

std::optional<TextureRead> common_texture_read(const ItemModelFiles& files, std::uint8_t& resident) {
    if ((resident & kResidentCommon) != 0) {
        return std::nullopt;
    }
    resident |= kResidentCommon;
    return TextureRead{TextureSheet::Common, files.common_texture};
}

std::vector<TextureRead> map_texture_reads(const ItemModelFiles& files, std::string_view map,
                                           std::uint8_t& resident) {
    std::vector<TextureRead> reads;
    if ((resident & kResidentModel) == 0) {
        reads.push_back({TextureSheet::Model, files.model_texture});
        resident |= kResidentModel;
    }
    const MapItemPack* pack = map_pack(files, map);
    if (pack == nullptr || pack->key_texture.empty()) {
        return reads;
    }
    const auto at = std::find(files.key_textures.begin(), files.key_textures.end(), pack->key_texture);
    const auto bit = static_cast<std::uint8_t>(1u << (1 + (at - files.key_textures.begin())));
    if ((resident & bit) == 0) {
        reads.push_back({TextureSheet::Key, pack->key_texture});
        resident = static_cast<std::uint8_t>((resident & ~kResidentKeys) | bit);
    }
    return reads;
}

}  // namespace sh::inventory
