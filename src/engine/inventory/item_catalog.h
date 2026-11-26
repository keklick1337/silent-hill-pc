// SPDX-License-Identifier: GPL-3.0-or-later
//
// The item data of the running game: gamedata/items/ and
// gamedata/weapons/ (sh::items::ItemLibrary) and the item model files of gamedata/menu/item_models.json,
// loaded once at boot, and the questions the item screens and the pickup ask of them: which file holds an
// item's close-up model, how its model is tilted and lit on screen, which items the results screen shows,
// which files a map's item pack and textures are.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/inventory/item_models.h"
#include "engine/items/item_library.h"

namespace sh::inventory {

// A line for the journal: what load() did, or a problem (warning).
struct CatalogMessage {
    bool warning = false;
    std::string text;
};

struct DisplayTilt {
    std::int16_t x = 0;  // Q12 angle
    std::int16_t z = 0;
};

class ItemCatalog {
public:
    // Reads the item library and the model files. Messages (warnings, what was loaded) go to `log`
    // as lines; the library stays absent when it cannot be read. Only the first successful load counts.
    void load(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
              std::vector<CatalogMessage>& log);
    bool loaded() const { return library_.has_value(); }
    // `mods reload`: forgets the library and the model files and loads them again. Pointers into the old library
    // die with it, so the game calls this only while no play state holds item definitions.
    void reload(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                std::vector<CatalogMessage>& log);

    // nullptr before load() or when the library could not be read.
    const items::ItemLibrary* library() const { return library_ ? &*library_ : nullptr; }
    const items::ItemDef* item(int number) const;
    const ItemModelFiles& model_files() const { return files_; }

    // The game file of the item's close-up model: items/<Name>.json "close_up_model", else the shared fallback.
    const std::string& close_up_model(int item) const;
    // items/<Name>.json "close_up_fix" ("" none).
    const std::string& close_up_fix(int item) const;
    // items/<Name>.json "display_lit": the model takes the item screen's lights.
    bool display_lit(int item) const;
    // items/<Name>.json "display_tilt"; zero for an unknown item.
    DisplayTilt display_tilt(int item) const;
    // items/<Name>.json "display_center" (a mod's own glb, its metres); nullptr: the middle of the model's bounds.
    const std::array<double, 3>* display_center(int item) const;
    // items/<Name>.json "display_scale" (a mod's own glb); 1 otherwise.
    double display_scale(int item) const;
    // The item number of the index-th award of the results screen; nullopt when there is none.
    std::optional<int> results_item(int index) const;
    // Units of `ammo` one pickup gives (1 without data); rounds per magazine of a gun (0 without data).
    int ammo_pickup_count(int ammo) const;
    int gun_clip(int weapon_number) const;
    // What the loaded data says an item in hand is (ItemLibrary::held_number, held_item, held_as_gun,
    // ammo_item_number); before load() nothing is held: -1, false.
    int held_number(int item) const { return library_ ? library_->held_number(item) : -1; }
    int held_item(int number) const { return library_ ? library_->held_item(number) : -1; }
    bool held_as_gun(int item) const { return library_ && library_->held_as_gun(item); }
    int ammo_item(int item) const { return library_ ? library_->ammo_item_number(item) : -1; }
    // The equip rig of the weapon with this number; nullptr when it has none.
    const items::WeaponRig* weapon_rig(int weapon_number) const;
    // The file of the map's item pack ("" none).
    const std::string& map_pack_models(std::string_view map) const;

private:
    std::optional<items::ItemLibrary> library_;
    ItemModelFiles files_;
    bool files_read_ = false;
};

// The catalog of the running game.
ItemCatalog& item_catalog();

// The name of map number `index` ("map1_s03"): the maps of an area are numbered one after the other. "" when out
// of range.
std::string map_name_of(int index);

}  // namespace sh::inventory
