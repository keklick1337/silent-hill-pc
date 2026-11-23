// SPDX-License-Identifier: GPL-3.0-or-later
// The "inventory" sections of gamedata/items/: every item names known commands
// and the base game's rules load as the original has them.

#include "engine/inventory/inventory.h"
#include "engine/items/item_library.h"

#include <filesystem>
#include <string>
#include <vector>

#include "framework/sh_test.h"

namespace {

using sh::inventory::Command;
using sh::inventory::command_from_name;

const sh::items::InventoryDef& inventory_of(const sh::items::ItemLibrary& library, const char* name)
{
    static const sh::items::InventoryDef none;
    const sh::items::ItemDef* item = library.find_item(name);
    return item != nullptr ? item->inventory : none;
}

}  // namespace

SH_TEST(gamedata_items_inventory_sections) {
    std::vector<std::string> warnings;
    const auto loaded = sh::items::ItemLibrary::load(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", warnings);
    SH_CHECK(loaded);
    const sh::items::ItemLibrary& library = loaded.value();
    for (const sh::items::ItemDef& item : library.items()) {
        SH_CHECK(command_from_name(item.inventory.command).has_value());
        SH_CHECK(item.inventory.command_in_hand.empty() || command_from_name(item.inventory.command_in_hand));
        for (const auto& [map, command] : item.inventory.command_on_maps) {
            SH_CHECK(map.starts_with("map") && command_from_name(command));
        }
    }
    SH_CHECK(inventory_of(library, "HealthDrink").stack_limit == 100);
    SH_CHECK(inventory_of(library, "HandgunBullets").stack_limit == 200);
    SH_CHECK(inventory_of(library, "Handgun").keep_when_empty);
    SH_CHECK(inventory_of(library, "Handgun").command == "EquipReload");
    SH_CHECK(inventory_of(library, "HyperBlaster").command_in_hand == "Unequip");
    SH_CHECK(inventory_of(library, "PocketRadio").command_on_maps.size() == 2);
    SH_CHECK(inventory_of(library, "ChannelingStone").pickup == "special");
    SH_CHECK(inventory_of(library, "ChannelingStone").pickup_once);
    SH_CHECK(inventory_of(library, "LobbyKey").order == 74);
    // The screen's name tags and item use.
    SH_CHECK(inventory_of(library, "Ampoule").name_tag == "stock");
    SH_CHECK(inventory_of(library, "Flashlight").name_tag == "switch");
    SH_CHECK(inventory_of(library, "HyperBlaster").name_tag == "hyper_blaster");
    SH_CHECK(inventory_of(library, "Shotgun").stock_in_hand && !inventory_of(library, "HyperBlaster").stock_in_hand);
    SH_CHECK(inventory_of(library, "LobbyKey").used_up_on_event && inventory_of(library, "GasolineTank").used_up_on_event);
    SH_CHECK(!inventory_of(library, "KitchenKnife").used_up_on_event && inventory_of(library, "Lighter").name_tag.empty());
    SH_CHECK(inventory_of(library, "Shotgun").usable_ammo == "loaded" && inventory_of(library, "RifleShells").usable_ammo == "with_gun");
    SH_CHECK(inventory_of(library, "HyperBlaster").unlock.enabled && inventory_of(library, "HyperBlaster").unlock.gun_controller);
    SH_CHECK(inventory_of(library, "HyperBlaster").unlock.endings == std::vector<std::string>{"Ufo"});
    SH_CHECK(!inventory_of(library, "Flauros").selectable && inventory_of(library, "Flashlight").selectable);
    SH_CHECK(inventory_of(library, "PocketRadio").switches == "radio");
    SH_CHECK(inventory_of(library, "KeyOfLion").examine_picture == "TIM/LION.TIM");
    SH_CHECK(inventory_of(library, "AntiqueShopKey").examine_flag.when == "M2S00_PickupMap");
    SH_CHECK(inventory_of(library, "Ampoule").fills_saturation && !inventory_of(library, "HealthDrink").fills_saturation);
    SH_CHECK(inventory_of(library, "UnknownLiquid").use_on_npc.map == "map6_s04" &&
             inventory_of(library, "UnknownLiquid").use_on_npc.flag == "M6S04_UnknownLiquidUsedOnCybil");
}

SH_TEST(item_inventory_section_patches_fields) {
    std::vector<std::string> warnings;
    const auto library = sh::items::ItemLibrary::parse(
        R"({"items": [{"id": 224, "enum": "InvItemId_Flashlight", "group": "portable",
            "inventory": {"order": 17, "command": "OnOff", "command_on_maps": {"map1_s00": "Unusable"},
                          "pickup": "special", "pickup_once": true}}]})",
        R"({"weapons": []})", warnings);
    SH_CHECK(library);
    const sh::items::InventoryDef& def = library.value().items().front().inventory;
    SH_CHECK(def.order == 17 && def.command == "OnOff" && def.pickup == "special" && def.pickup_once);
    SH_CHECK(def.command_on_maps.at("map1_s00") == "Unusable");
    SH_CHECK(def.stack_limit == 0 && !def.keep_when_empty && def.command_in_hand.empty());
    SH_CHECK(!sh::items::ItemLibrary::parse(R"({"items": [{"id": 224, "enum": "InvItemId_Flashlight", "inventory": {"pickup": "rare"}}]})",
                                            R"({"weapons": []})", warnings));
}

// display_tilt of items/<Name>.json (the item screen and pickup model tilt): the base game's
// values, and the Q12 angle conversion truncates like the original's constants.
SH_TEST(gamedata_items_display_tilt) {
    std::vector<std::string> warnings;
    const auto loaded = sh::items::ItemLibrary::load(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", warnings);
    SH_CHECK(loaded);
    const sh::items::ItemLibrary& library = loaded.value();
    const auto tilt = [&](const char* name) {
        const sh::items::ItemDef* item = library.find_item(name);
        return std::pair{item ? sh::items::angle_q12(item->display_tilt_x) : std::int16_t(-1),
                         item ? sh::items::angle_q12(item->display_tilt_z) : std::int16_t(-1)};
    };
    SH_CHECK((tilt("Handgun") == std::pair<std::int16_t, std::int16_t>{512, 0}));
    SH_CHECK((tilt("HealthDrink") == std::pair<std::int16_t, std::int16_t>{512, 0}));
    SH_CHECK((tilt("Chemical") == std::pair<std::int16_t, std::int16_t>{-512, 0}));
    SH_CHECK((tilt("PocketRadio") == std::pair<std::int16_t, std::int16_t>{2560, 2048}));
    SH_CHECK((tilt("CutsceneBaby") == std::pair<std::int16_t, std::int16_t>{0, 0}));
    SH_CHECK(sh::items::angle_q12(0.5) == 5 && sh::items::angle_q12(120.0) == 1365);
    int tilted = 0;
    for (const sh::items::ItemDef& item : library.items()) {
        tilted += item.display_tilt_x != 0.0 || item.display_tilt_z != 0.0;
    }
    SH_CHECK(tilted == 75);
}
