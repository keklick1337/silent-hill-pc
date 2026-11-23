// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/menu/item_models.json and items/<Name>.json "close_up_model": which files hold the item
// models and textures, and the texture residency the map changes walk through.

#include "engine/inventory/item_models.h"
#include "engine/items/item_library.h"

#include <filesystem>
#include <string>
#include <vector>

#include "framework/sh_test.h"

namespace {

namespace inv = sh::inventory;

inv::ItemModelFiles shipped()
{
    auto loaded = inv::load_item_models(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {});
    SH_CHECK(loaded);
    return loaded ? loaded.value() : inv::ItemModelFiles{};
}

std::vector<std::string> files_of(const std::vector<inv::TextureRead>& reads)
{
    std::vector<std::string> out;
    for (const inv::TextureRead& r : reads) {
        out.push_back(r.file);
    }
    return out;
}

}  // namespace

SH_TEST(item_models_shipped_maps) {
    const inv::ItemModelFiles files = shipped();
    SH_CHECK(files.close_up_fallback == "ITEM/UNQ20.TMD");
    SH_CHECK(files.key_textures.size() == 6);
    SH_CHECK(files.maps.size() == 39);
    const inv::MapItemPack* school = inv::map_pack(files, "map1_s02");
    SH_CHECK(school != nullptr && school->models == "ITEM/IT_002.TMD" && school->key_texture == "ITEM/TIM01.TIM");
    // The first map has its pack but no key sheet; map1_s04 and map2_s03 have neither.
    const inv::MapItemPack* first = inv::map_pack(files, "map0_s00");
    SH_CHECK(first != nullptr && first->models == "ITEM/IT_000.TMD" && first->key_texture.empty());
    SH_CHECK(inv::map_pack(files, "map1_s04") == nullptr && inv::map_pack(files, "map2_s03") == nullptr);
}

SH_TEST(item_models_texture_residency) {
    const inv::ItemModelFiles files = shipped();
    std::uint8_t resident = 0;
    SH_CHECK((files_of(inv::map_texture_reads(files, "map1_s00", resident)) ==
              std::vector<std::string>{"ITEM/TIM07.TIM", "ITEM/TIM01.TIM"}));
    SH_CHECK(resident == (inv::kResidentModel | 0x02));
    // Same sheet: nothing to read. Another area's sheet evicts it, the common and model bits stay.
    SH_CHECK(inv::map_texture_reads(files, "map1_s05", resident).empty());
    SH_CHECK(inv::common_texture_read(files, resident)->file == "ITEM/TIM00.TIM");
    SH_CHECK(!inv::common_texture_read(files, resident));
    SH_CHECK((files_of(inv::map_texture_reads(files, "map7_s03", resident)) == std::vector<std::string>{"ITEM/TIM06.TIM"}));
    SH_CHECK(resident == (inv::kResidentModel | inv::kResidentCommon | 0x40));
    // No key sheet for the map (or no entry): the resident one stays.
    SH_CHECK(inv::map_texture_reads(files, "map0_s00", resident).empty());
    SH_CHECK(inv::map_texture_reads(files, "", resident).empty());
    SH_CHECK(resident == (inv::kResidentModel | inv::kResidentCommon | 0x40));
}

SH_TEST(item_models_parse_errors) {
    SH_CHECK(!inv::parse_item_models(R"({"maps": {"m": {"key_texture": "X.TIM"}}})"));
    SH_CHECK(!inv::parse_item_models(R"({"key_textures": ["1","2","3","4","5","6","7"]})"));
    SH_CHECK(!inv::parse_item_models(R"({"maps": {"m": {"model": "X.TMD"}}})"));
    SH_CHECK(inv::parse_item_models(R"({"key_textures": ["K.TIM"], "maps": {"m": {"key_texture": "K.TIM"}}})"));
}

SH_TEST(gamedata_items_close_up_models) {
    std::vector<std::string> warnings;
    const auto loaded = sh::items::ItemLibrary::load(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", warnings);
    SH_CHECK(loaded);
    int named = 0;
    for (const sh::items::ItemDef& item : loaded.value().items()) {
        if (!item.close_up_model.empty()) {
            named++;
            SH_CHECK(item.close_up_model.starts_with("ITEM/UNQ") && item.close_up_model.ends_with(".TMD"));
        }
    }
    SH_CHECK(named == 73);
    // The sewer's two keys share one model; the first aid kit has none of its own.
    SH_CHECK(loaded.value().find_item("SewerKey")->close_up_model == "ITEM/UNQ4D.TMD");
    SH_CHECK(loaded.value().find_item("SewerExitKey")->close_up_model == "ITEM/UNQ4D.TMD");
    SH_CHECK(loaded.value().find_item("FirstAidKit")->close_up_model.empty());
}

SH_TEST(gamedata_items_display_lit) {
    std::vector<std::string> warnings;
    const auto loaded = sh::items::ItemLibrary::load(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", warnings);
    SH_CHECK(loaded);
    int lit = 0;
    for (const sh::items::ItemDef& item : loaded.value().items()) {
        lit += item.display_lit;
    }
    SH_CHECK(lit == 16);
    SH_CHECK(loaded.value().find_item("Handgun")->display_lit);
    SH_CHECK(!loaded.value().find_item("Katana")->display_lit && !loaded.value().find_item("RockDrill")->display_lit);
}
