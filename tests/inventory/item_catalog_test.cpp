// SPDX-License-Identifier: GPL-3.0-or-later
// The item catalog (sh::inventory::ItemCatalog, item_catalog.h) over the shipped gamedata: close-up models,
// display rules, the results awards and the map names of the map numbers.

#include "engine/inventory/item_catalog.h"

#include <filesystem>
#include <vector>

#include "framework/sh_test.h"

namespace {

namespace inv = sh::inventory;

const inv::ItemCatalog& shipped()
{
    static const inv::ItemCatalog s_catalog = [] {
        inv::ItemCatalog catalog;
        std::vector<inv::CatalogMessage> log;
        catalog.load(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, log);
        return catalog;
    }();
    return s_catalog;
}

std::uint8_t number_of(const char* name)
{
    const sh::items::ItemDef* item = shipped().library()->find_item(name);
    return item != nullptr ? static_cast<std::uint8_t>(item->number) : 0;
}

}  // namespace

SH_TEST(item_catalog_map_names) {
    SH_CHECK(inv::map_name_of(0) == "map0_s00");
    SH_CHECK(inv::map_name_of(2) == "map0_s02");
    SH_CHECK(inv::map_name_of(3) == "map1_s00");
    SH_CHECK(inv::map_name_of(10) == "map2_s00");
    SH_CHECK(inv::map_name_of(42) == "map7_s03");
    SH_CHECK(inv::map_name_of(43).empty());
    SH_CHECK(inv::map_name_of(-1).empty());
}

SH_TEST(item_catalog_shipped_items) {
    const inv::ItemCatalog& catalog = shipped();
    SH_CHECK(catalog.loaded());
    const std::uint8_t drink = number_of("HealthDrink");
    SH_CHECK(drink != 0);
    SH_CHECK(catalog.close_up_model(drink) == "ITEM/UNQ21.TMD");
    SH_CHECK(catalog.display_lit(drink));
    SH_CHECK(catalog.display_tilt(drink).x == sh::items::angle_q12(45.0));
    SH_CHECK(catalog.display_tilt(drink).z == 0);
    SH_CHECK(catalog.close_up_fix(number_of("UnknownLiquid")) == "liquid_uvs");
    // An unknown number: the shared fallback model, no tilt, unlit.
    SH_CHECK(catalog.close_up_model(0xFE) == catalog.model_files().close_up_fallback);
    SH_CHECK(!catalog.display_lit(0xFE) && catalog.display_tilt(0xFE).x == 0);
    SH_CHECK(catalog.ammo_pickup_count(0xFE) == 1);
}

SH_TEST(item_catalog_results_awards) {
    const inv::ItemCatalog& catalog = shipped();
    SH_CHECK(catalog.results_item(0) == number_of("GasolineTank"));
    SH_CHECK(catalog.results_item(5) == number_of("HyperBlaster"));
    SH_CHECK(!catalog.results_item(6).has_value());
    SH_CHECK(!catalog.results_item(-1).has_value());
    SH_CHECK(catalog.map_pack_models("map0_s01") == "ITEM/IT_001.TMD");
    SH_CHECK(catalog.map_pack_models("map9_s99").empty());
}

SH_TEST(item_catalog_empty_before_load) {
    inv::ItemCatalog catalog;
    SH_CHECK(catalog.library() == nullptr && catalog.item(0x80) == nullptr);
    SH_CHECK(catalog.gun_clip(1) == 0 && catalog.ammo_pickup_count(0x80) == 1);
    SH_CHECK(catalog.weapon_rig(1) == nullptr && !catalog.results_item(0).has_value());
}
