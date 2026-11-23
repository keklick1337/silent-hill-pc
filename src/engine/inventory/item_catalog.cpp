// SPDX-License-Identifier: GPL-3.0-or-later

#include "engine/inventory/item_catalog.h"

#include <cstdio>
#include <utility>

namespace sh::inventory {

namespace {

const std::string kNone;

}  // namespace

void ItemCatalog::load(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                       std::vector<CatalogMessage>& log)
{
    if (!files_read_)
    {
        files_read_ = true;
        std::string source;
        Result<ItemModelFiles> files = load_item_models(gamedata_dir, mods_dir, &source);
        if (files)
        {
            files_ = std::move(files.value());
            log.push_back({false, source});
        }
        else
        {
            log.push_back({true, files.error() + " - no item model files"});
        }
    }
    if (library_)
        return;

    std::vector<std::string> warnings;
    items::LoadSource source;
    Result<items::ItemLibrary> library = items::ItemLibrary::load(gamedata_dir, mods_dir, warnings, &source);
    if (!library)
    {
        log.push_back({true, library.error() + " (keeping the compiled-in values)"});
        return;
    }
    for (std::string& warning : warnings)
        log.push_back({true, std::move(warning)});
    for (std::string& problem : library.value().check_consistency())
        log.push_back({true, std::move(problem)});
    library_.emplace(std::move(library.value()));
    const std::string from = source.mod_id.empty() ? std::string() : " (mod " + source.mod_id + ")";
    log.push_back({false, "gamedata/items: " + std::to_string(library_->item_count()) + " items, " +
                  std::to_string(library_->weapon_count()) + " weapons" + from});
}

void ItemCatalog::reload(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                         std::vector<CatalogMessage>& log)
{
    files_read_ = false;
    files_ = ItemModelFiles{};
    library_.reset();
    load(gamedata_dir, mods_dir, log);
}

const items::ItemDef* ItemCatalog::item(int number) const
{
    return library_ ? library_->find_item(number) : nullptr;
}

const std::string& ItemCatalog::close_up_model(int number) const
{
    const items::ItemDef* def = item(number);
    return def != nullptr && !def->close_up_model.empty() ? def->close_up_model : files_.close_up_fallback;
}

const std::string& ItemCatalog::close_up_fix(int number) const
{
    const items::ItemDef* def = item(number);
    return def != nullptr ? def->close_up_fix : kNone;
}

bool ItemCatalog::display_lit(int number) const
{
    const items::ItemDef* def = item(number);
    return def != nullptr && def->display_lit;
}

DisplayTilt ItemCatalog::display_tilt(int number) const
{
    const items::ItemDef* def = item(number);
    if (def == nullptr)
        return {};
    return {items::angle_q12(def->display_tilt_x), items::angle_q12(def->display_tilt_z)};
}

std::optional<int> ItemCatalog::results_item(int index) const
{
    if (!library_ || index < 0 || index >= static_cast<int>(files_.results_items.size()))
        return std::nullopt;
    const items::ItemDef* def = library_->find_item(files_.results_items[static_cast<std::size_t>(index)]);
    if (def == nullptr || def->number == 0)
        return std::nullopt;
    return def->number;
}

int ItemCatalog::ammo_pickup_count(int ammo) const
{
    return library_ ? library_->ammo_pickup_count(ammo) : 1;
}

int ItemCatalog::gun_clip(int weapon_number) const
{
    return library_ ? library_->clip_capacity(weapon_number) : 0;
}

const items::WeaponRig* ItemCatalog::weapon_rig(int weapon_number) const
{
    if (!library_)
        return nullptr;
    const items::WeaponDef* weapon = library_->find_weapon(weapon_number);
    return weapon != nullptr && weapon->rig.present ? &weapon->rig : nullptr;
}

const std::string& ItemCatalog::map_pack_models(std::string_view map) const
{
    const MapItemPack* pack = map_pack(files_, map);
    return pack != nullptr ? pack->models : kNone;
}

ItemCatalog& item_catalog()
{
    static ItemCatalog s_catalog;
    return s_catalog;
}

std::string map_name_of(int index)
{
    static constexpr int kMapsPerArea[] = {3, 7, 5, 7, 7, 4, 6, 4};
    for (int area = 0; area < static_cast<int>(std::size(kMapsPerArea)) && index >= 0; ++area)
    {
        if (index < kMapsPerArea[area])
        {
            char name[16];
            std::snprintf(name, sizeof(name), "map%d_s%02d", area, index);
            return name;
        }
        index -= kMapsPerArea[area];
    }
    return {};
}

}  // namespace sh::inventory
