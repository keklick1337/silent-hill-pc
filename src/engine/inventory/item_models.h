// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Which game files hold the inventory's item models and textures (gamedata/menu/item_models.json):
// the close-up model an item without one of its own falls back to (items/<Name>.json
// "close_up_model" names the rest), the texture sheets every area shares, and per map the item pack
// (one model file with every item the map can hold) and the key-item texture sheet it needs; the
// items the results screen shows.
//
// The textures stay resident across map changes. Residency is one byte the game keeps with its
// state: bit 0 the common sheet, bits 1..6 the key sheets (key_textures[i] is bit 1 + i, only one
// is resident at a time: they share one place in video memory), bit 7 the model sheet. Standard
// library plus the shared JSON and mod helpers.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::inventory {

struct MapItemPack {
    std::string key_texture;  // one of ItemModelFiles::key_textures; empty: the map needs none
    std::string models;       // the item pack; empty: the map has none
};

struct ItemModelFiles {
    std::string close_up_fallback;          // close-up model of an item that names none
    std::string common_texture;             // read once, on demand
    std::string model_texture;              // read once, with the first map pack
    std::vector<std::string> key_textures;  // at most kMaxKeyTextures
    std::map<std::string, MapItemPack, std::less<>> maps;
    // The items the results screen shows, one model each in order (the awards of the ranking).
    std::vector<std::string> results_items;
};

inline constexpr std::size_t kMaxKeyTextures = 6;
inline constexpr std::size_t kResultsItems = 6;
inline constexpr std::uint8_t kResidentCommon = 1u << 0;
inline constexpr std::uint8_t kResidentModel = 1u << 7;
inline constexpr std::uint8_t kResidentKeys = 0x7E;  // bits 1..6

enum class TextureSheet : std::uint8_t { Common, Model, Key };

struct TextureRead {
    TextureSheet sheet = TextureSheet::Common;
    std::string file;
};

Result<ItemModelFiles> parse_item_models(std::string_view json_text);

// Reads menu/item_models.json from the last mod (in load order) that has it, else from the base
// gamedata. `source` receives the path read.
Result<ItemModelFiles> load_item_models(const std::filesystem::path& gamedata_dir,
                                        const std::filesystem::path& mods_dir, std::string* source = nullptr);

// The pack of `map`, nullptr when the file lists none for it.
const MapItemPack* map_pack(const ItemModelFiles& files, std::string_view map);

// The common sheet when it is not resident yet (and marks it resident).
std::optional<TextureRead> common_texture_read(const ItemModelFiles& files, std::uint8_t& resident);

// The sheets entering `map` needs that are not resident, in read order (the model sheet first,
// then the map's key sheet, which evicts the key sheet resident before); updates `resident`.
std::vector<TextureRead> map_texture_reads(const ItemModelFiles& files, std::string_view map,
                                           std::uint8_t& resident);

}  // namespace sh::inventory
