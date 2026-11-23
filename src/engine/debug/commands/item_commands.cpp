// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Console commands over what Harry carries: give, equip, givemap, saturate. The short names `give` takes and
// what comes along with each are data: gamedata/config/console_give.json.

#include "engine/debug/commands/command_groups.h"
#include "engine/debug/commands/command_kit.h"
#include "engine/game_state/game_state.h"
#include "engine/ids/game_numbers.h"
#include "engine/ids/ids.h"
#include "engine/inventory/game/inventory_host.h"
#include "engine/inventory/item_catalog.h"
#include "engine/items/item_library.h"
#include "engine/log/sh_log.h"
#include "engine/mods/data_reload.h"
#include "engine/runtime/game_hooks.h"
#include "engine/vfs/vfs_data_json.h"
#include "formats/common/json.h"
#include "input/pc_quick_actions.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace sh::debug::commands {

namespace {

namespace gs = sh::game_state;

constexpr std::int32_t kFullHealth = 100 * 4096;

// ---- gamedata/config/console_give.json ----

struct Portion {
    int item = 0;
    int count = 1;
};

struct GiveAlias {
    std::string alias;  // as typed, in capitals
    Portion what;
    bool is_weapon = false;  // a weapons/ file carries the item
    bool unlocks_hyper_blaster = false;
    std::vector<Portion> along;
};

struct GiveTable {
    std::vector<GiveAlias> aliases;
    std::vector<Portion> ammo;
    std::vector<Portion> all_weapons_extra;
};

const sh::items::ItemLibrary* item_library() { return sh::inventory::item_catalog().library(); }

int item_of(const sh::json::Value& entry) {
    const sh::json::Value* name = entry.find("item");
    return name != nullptr && name->is_string() ? sh::ids::item_number(name->string().c_str()) : -1;
}

int count_of(const sh::json::Value& entry) {
    const sh::json::Value* count = entry.find("count");
    return count != nullptr && count->is_integer() ? static_cast<int>(count->integer()) : 1;
}

std::vector<Portion> portions(const sh::json::Value* list) {
    std::vector<Portion> out;
    if (list == nullptr || !list->is_array()) return out;
    for (const sh::json::Value& entry : list->array()) {
        const int item = item_of(entry);
        if (item <= 0) {
            const sh::json::Value* name = entry.find("item");
            SH_ERR("[CONSOLE] console_give.json: unknown item %s",
                   name != nullptr && name->is_string() ? name->string().c_str() : "(none)");
            continue;
        }
        out.push_back({item, count_of(entry)});
    }
    return out;
}

GiveTable read_give_table() {
    GiveTable table;
    const std::optional<std::string> text = ShVfs_ReadDataJsonText("config/console_give.json");
    const Result<sh::json::Value> document = text ? sh::json::parse(*text) : Result<sh::json::Value>::fail("missing");
    if (!document || !document.value().is_object()) {
        SH_ERR("[CONSOLE] gamedata/config/console_give.json: %s; GIVE knows no items",
               document ? "not an object" : document.error().c_str());
        return table;
    }
    const sh::json::Value& root = document.value();
    const sh::items::ItemLibrary* library = item_library();
    if (const sh::json::Value* items = root.find("items"); items != nullptr && items->is_array()) {
        for (const sh::json::Value& entry : items->array()) {
            const sh::json::Value* alias = entry.find("alias");
            const int item = item_of(entry);
            if (alias == nullptr || !alias->is_string() || item <= 0) {
                SH_ERR("[CONSOLE] console_give.json: an entry needs an \"alias\" and a known \"item\"");
                continue;
            }
            GiveAlias row;
            row.alias = alias->string();
            row.what = {item, count_of(entry)};
            row.is_weapon = library != nullptr && library->weapon_of_item(static_cast<std::uint16_t>(item)) != nullptr;
            const sh::json::Value* unlock = entry.find("unlock_hyper_blaster");
            row.unlocks_hyper_blaster = unlock != nullptr && unlock->is_bool() && unlock->boolean();
            row.along = portions(entry.find("with"));
            table.aliases.push_back(std::move(row));
        }
    }
    table.ammo = portions(root.find("ammo"));
    table.all_weapons_extra = portions(root.find("all_weapons_extra"));
    return table;
}

const GiveTable& give_table() {
    static GiveTable table;
    static std::uint32_t seen = sh::mods::kNeverRead;
    if (sh::mods::data_stale(seen)) table = read_give_table();
    return table;
}

// Health, ammo and portable items add up; anything else is given only when Harry does not hold it yet.
void hand_over(const Portion& portion) {
    const sh::items::ItemDef* def = sh::inventory::game::item_def(static_cast<std::uint8_t>(portion.item));
    const bool adds_up = def != nullptr && (def->group == "health" || def->group == "ammo" || def->group == "portable");
    if (!adds_up && gs::inventory_has(portion.item)) return;
    const int given = sh::hooks::game_item_given(portion.item, portion.count != 0 ? portion.count : 1, "console");
    if (given > 0) gs::inventory_add(portion.item, given);
}

// An item by its own name ("mod:Name" or the short name, any case): a mod's item needs no alias.
const sh::items::ItemDef* item_named(const std::string& name) {
    const sh::items::ItemLibrary* library = item_library();
    if (library == nullptr) return nullptr;
    for (const sh::items::ItemDef& item : library->items()) {
        if (item.number != 0 && (same_word(name, item.id) || same_word(name, sh::ids::local_name(item.id)))) {
            return &item;
        }
    }
    return nullptr;
}

Result<void> run_give(Context& ctx, const Args& args) {
    const std::string what = upper(args.joined(0));
    const GiveTable& table = give_table();
    if (what.empty()) {
        say(ctx, "give <item> - see 'help give' for the list");
    } else if (what == "HEALTH") {
        gs::set_chara_health(0, kFullHealth);
        say(ctx, "Health restored");
    } else if (what == "AMMO") {
        for (const Portion& portion : table.ammo) hand_over(portion);
        say(ctx, "Given the ammo of console_give.json");
    } else if (what == "ALLWEAPONS") {
        for (const GiveAlias& row : table.aliases) {
            if (row.is_weapon) hand_over({row.what.item, 1});
        }
        for (const Portion& portion : table.all_weapons_extra) hand_over(portion);
        // Without its unlock the inventory disables the Hyper Blaster's aim, so a given blaster could not fire.
        gs::unlock_hyperblaster();
        say(ctx, "Given all weapons + ammo + gas");
    } else {
        for (const GiveAlias& row : table.aliases) {
            if (row.alias != what) continue;
            hand_over(row.what);
            for (const Portion& portion : row.along) hand_over(portion);
            if (row.unlocks_hyper_blaster) gs::unlock_hyperblaster();
            say(ctx, "Given %s", row.alias.c_str());
            return done();
        }
        if (const sh::items::ItemDef* item = item_named(what)) {  // one pickup's worth
            hand_over({item->number, item->pickup_count});
            say(ctx, "Given %s", item->id.c_str());
        } else {
            say(ctx, "unknown item '%s' - see 'help give'", what.c_str());
        }
    }
    return done();
}

Result<void> run_equip(Context& ctx, const Args& args) {
    const std::string weapon = upper(args.joined(0));
    bool known = false;
    for (const GiveAlias& row : give_table().aliases) {
        if (!row.is_weapon || row.alias != weapon) continue;
        known = true;
        say(ctx, Pc_EquipOwnedWeapon(row.what.item) ? "Equipped %s" : "cannot equip %s now", weapon.c_str());
    }
    if (!known) {  // any weapon's item by its name (a mod's gun)
        const sh::items::ItemDef* item = item_named(weapon);
        const sh::items::ItemLibrary* library = item_library();
        if (item != nullptr && library != nullptr && library->weapon_of_item(item->number) != nullptr) {
            known = true;
            say(ctx, Pc_EquipOwnedWeapon(item->number) ? "Equipped %s" : "cannot equip %s now", item->id.c_str());
        }
    }
    if (!known) say(ctx, "equip <weapon> - a weapon name of 'help give' or a weapon's item name");
    return done();
}

std::vector<std::string> alias_names(bool weapons_only) {
    std::vector<std::string> names;
    for (const GiveAlias& row : give_table().aliases) {
        if (!weapons_only || row.is_weapon) names.push_back(lower(row.alias));
    }
    return names;
}

void complete_items(std::string_view, std::vector<Candidate>& out) {
    static std::vector<Candidate> cache;
    if (cache.empty()) {
        cache.push_back({"health", "refill Harry's health"});
        cache.push_back({"ammo", "30 of each ammo"});
        cache.push_back({"allweapons", "every weapon, ammo and fuel"});
        for (const std::string& name : alias_names(false)) cache.push_back({name, {}});
    }
    out.insert(out.end(), cache.begin(), cache.end());
}

// ---- area maps ----
//
// The area maps are bits of the save, not inventory items, so they have a command of their own. Their names
// are the pages of gamedata/paper_maps.json, in the game's page order.

std::string area_map_name(int page) {
    const char* id = sh::ids::paper_map_name(page);
    return id != nullptr ? std::string(sh::ids::local_name(id)) : std::string();
}

void complete_area_maps(std::string_view, std::vector<Candidate>& out) {
    static std::vector<Candidate> cache;
    if (cache.empty()) {
        cache.push_back({"list", "area maps, * = held"});
        cache.push_back({"all", "grant every area map"});
        for (int page = 0; page < gs::kPaperMaps; ++page) cache.push_back({area_map_name(page), {}});
    }
    out.insert(out.end(), cache.begin(), cache.end());
}

Result<void> run_givemap(Context& ctx, const Args& args) {
    const std::string what = upper(args.joined(0));
    if (what == "LIST") {
        LineWrapper list(ctx, 200, 8, " ");
        for (int page = 0; page < gs::kPaperMaps; ++page) {
            const std::string name = area_map_name(page);
            list.add(std::to_string(page) + ":" + name + (gs::paper_map_held(page) ? "*" : ""), name.size());
        }
        list.flush();
        say(ctx, "givemap list - * = already held");
        return done();
    }
    if (what == "ALL") {
        int added = 0;
        for (int page = 0; page < gs::kPaperMaps; ++page) {
            if (gs::paper_map_held(page)) continue;
            gs::grant_paper_map(page);
            ++added;
        }
        say(ctx, "gave %d map(s) - all areas", added);
        return done();
    }

    int page = 0;
    int typed_number = 0;
    if (what.empty()) {
        // The area Harry stands in; areas without a map of their own all carry the Other Places tag.
        page = gs::save_paper_map();
        if (page == gs::kPaperMapOtherPlaces) {
            say(ctx, "this area has no map of its own (Other Places)");
            return done();
        }
        if (page < 0 || page >= gs::kPaperMaps) {
            say(ctx, "area has no valid map (paperMapIdx %d)", page);
            return done();
        }
    } else if (std::sscanf(what.c_str(), "%d", &typed_number) == 1) {
        if (typed_number < 0 || typed_number >= gs::kPaperMaps) {
            say(ctx, "map index out of range (0-%d)", gs::kPaperMaps - 1);
            return done();
        }
        page = typed_number;
    } else {
        while (page < gs::kPaperMaps && !same_word(what, area_map_name(page))) ++page;
        if (page >= gs::kPaperMaps) {
            say(ctx, "unknown map '%s' - try 'givemap list'", what.c_str());
            return done();
        }
    }

    const std::string name = area_map_name(page);
    if (gs::paper_map_held(page)) {
        say(ctx, "already have %s (%d)", name.c_str(), page);
    } else {
        gs::grant_paper_map(page);
        say(ctx, "gave map: %s (%d)", name.c_str(), page);
    }
    return done();
}

}  // namespace

void register_item_commands(Registry& registry) {
    add(registry, "give", "game", "give an item, weapon, ammo or recovery",
        {arg("item", ArgType::String, false, "item name", {}, complete_items)}, run_give, {},
        "Guns come with ammo. Special names: health (refill), ammo (30 of each), allweapons (all weapons, ammo and "
        "fuel).\nStackable items add up; unique items are given once.");

    add(registry, "equip", "game", "take an owned weapon in hand",
        {arg("weapon", ArgType::String, false, "weapon name", {}, words_completion(alias_names(true)))}, run_equip);

    add(registry, "givemap", "game", "give an area map",
        {arg("map", ArgType::String, true, "area name or number, list, all; omitted: the current area", {},
             complete_area_maps)},
        run_givemap);

    // For scenarios: the health still to come back, as after an ampoule.
    add(registry, "saturate", "debug", "Health still to come back (an ampoule's saturation)",
        {opt_number("points", "")}, [](Context& ctx, const Args& args) {
            const double points = real(args.str(0));
            gs::set_health_saturation(static_cast<std::int32_t>(points * 4096.0));
            say(ctx, "saturation %.1f", points);
            return done();
        });
}

}  // namespace sh::debug::commands
