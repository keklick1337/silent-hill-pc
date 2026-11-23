// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/load_boot.h"

#include "formats/common/json.h"

#include <algorithm>

namespace sh::gameflow {

namespace {

bool name_matches(const std::string& have, std::string_view want) {
    return have == want;
}

}  // namespace

// ---- savegame_initialize ---------------------------------------------------------

Result<SaveInitParams> SaveInitParams::parse(std::string_view json) {
    const Result<json::Value> doc = json::parse(json);
    if (!doc) {
        return Result<SaveInitParams>::fail("load_boot.json: " + doc.error());
    }
    SaveInitParams out;
    const json::Value* section = doc.value().find("save_init");
    if (section == nullptr) {
        return Result<SaveInitParams>::ok(out);  // keeps the built-ins
    }
    struct Field {
        const char* name;
        std::int32_t SaveInitParams::*member;
    };
    static constexpr Field kFields[] = {
        {"difficulty_min", &SaveInitParams::difficulty_min},
        {"difficulty_max", &SaveInitParams::difficulty_max},
        {"enemy_state_count", &SaveInitParams::enemy_state_count},
        {"enemy_state_value", &SaveInitParams::enemy_state_value},
        {"start_paper_map", &SaveInitParams::start_paper_map},
    };
    for (const Field& f : kFields) {
        const json::Value* v = section->find(f.name);
        if (v == nullptr) {
            continue;
        }
        if (!v->is_integer()) {
            return Result<SaveInitParams>::fail(std::string("load_boot.json: \"") + f.name +
                                                "\" must be an integer");
        }
        out.*(f.member) = static_cast<std::int32_t>(v->integer());
    }
    return Result<SaveInitParams>::ok(out);
}

void savegame_initialize(SaveInitHost& host, std::int32_t overlay_id, std::int32_t difficulty,
                         const SaveInitParams& params) {
    host.savegame_clear();
    host.map_index_set(overlay_id);

    // The original's CLAMP into the difficulty table's bounds.
    if (difficulty < params.difficulty_min) {
        difficulty = params.difficulty_min;
    }
    if (difficulty > params.difficulty_max) {
        difficulty = params.difficulty_max;
    }

    host.difficulty_set(difficulty);
    host.paper_map_set(params.start_paper_map);

    // Every overlay enemy starts "alive". The original walked the state array
    // downwards from its end writing the same value — a fill.
    for (std::int32_t i = params.enemy_state_count - 1; i >= 0; i--) {
        host.enemy_state_set(i, params.enemy_state_value);
    }

    host.player_reset();
}

// ---- map_entry gates ---------------------------------------------------------------

MapEntryParams MapEntryParams::builtin() {
    MapEntryParams out;
    out.tutorial_maps = {"map0_s00", "map0_s01"};
    const StartLoadoutEntry entries[5] = {
        {"Flashlight", 1}, {"PocketRadio", 1}, {"KitchenKnife", 1}, {"Handgun", 1}, {"HandgunBullets", 15},
    };
    out.items.assign(entries, entries + 5);
    out.equipped = "Handgun";
    out.toggle_set = "radio_on";
    out.toggle_clear = "flashlight_off";
    return out;
}

Result<MapEntryParams> MapEntryParams::parse(std::string_view json) {
    const Result<json::Value> doc = json::parse(json);
    if (!doc) {
        return Result<MapEntryParams>::fail("load_boot.json: " + doc.error());
    }
    MapEntryParams out;
    const json::Value* section = doc.value().find("map_entry");
    if (section == nullptr) {
        return Result<MapEntryParams>::ok(builtin());  // a missing section keeps the built-ins
    }
    if (const json::Value* maps = section->find("tutorial_maps"); maps != nullptr && maps->is_array()) {
        out.tutorial_maps.clear();
        for (const json::Value& m : maps->array()) {
            if (m.is_string()) {
                out.tutorial_maps.push_back(m.string());
            }
        }
    }
    const json::Value* loadout = section->find("start_loadout");
    if (loadout != nullptr) {
        if (const json::Value* items = loadout->find("items"); items != nullptr && items->is_array()) {
            out.items.clear();
            for (const json::Value& e : items->array()) {
                const json::Value* item = e.find("item");
                const json::Value* count = e.find("count");
                if (item == nullptr || !item->is_string()) {
                    return Result<MapEntryParams>::fail("load_boot.json: every loadout item needs an \"item\" name");
                }
                StartLoadoutEntry entry;
                entry.item = item->string();
                if (count != nullptr && count->is_integer()) {
                    entry.count = static_cast<std::int32_t>(count->integer());
                }
                out.items.push_back(std::move(entry));
            }
        }
        if (const json::Value* v = loadout->find("equipped"); v != nullptr && v->is_string()) {
            out.equipped = v->string();
        }
        if (const json::Value* v = loadout->find("toggle_set"); v != nullptr && v->is_string()) {
            out.toggle_set = v->string();
        }
        if (const json::Value* v = loadout->find("toggle_clear"); v != nullptr && v->is_string()) {
            out.toggle_clear = v->string();
        }
    }
    return Result<MapEntryParams>::ok(out);
}

namespace {

bool is_tutorial_map(const MapEntryHost& host, std::int32_t map_idx, const MapEntryParams& params) {
    const char* name = host.map_name(map_idx);
    if (name == nullptr) {
        return false;
    }
    for (const std::string& m : params.tutorial_maps) {
        if (name_matches(m, name)) {
            return true;
        }
    }
    return false;
}

}  // namespace

void map_entry_spawn_gate(MapEntryHost& host, std::int32_t map_idx, const MapEntryParams& params) {
    if (host.force_enemy_spawn() && !is_tutorial_map(host, map_idx, params) && !host.boot_demo()) {
        host.no_spawn_flag_clear();
    }
}

void map_entry_start_loadout(MapEntryHost& host, std::int32_t map_idx, const MapEntryParams& params) {
    if (is_tutorial_map(host, map_idx, params) || !host.new_game() || !host.inventory_empty()) {
        return;
    }

    for (std::size_t i = 0; i < params.items.size(); i++) {
        const std::int32_t item = host.item_number(params.items[i].item);
        std::int32_t count = params.items[i].count;
        if (item < 0) {
            continue;  // unknown name: the slot stays as it was (empty marker)
        }
        host.item_slot_set(static_cast<std::int32_t>(i), item, count);
    }

    // The recount below only derives the weapon fields when a weapon is
    // equipped, so equip first; the equipped item of the inventory screen is
    // rebuilt from this field, not kept here.
    if (const std::int32_t equipped = host.item_number(params.equipped); equipped >= 0) {
        host.equipped_set(equipped);
    }
    if (const std::int32_t flag = host.toggle_number(params.toggle_set); flag >= 0) {
        host.toggle_flag_set(flag);
    }
    if (const std::int32_t flag = host.toggle_number(params.toggle_clear); flag >= 0) {
        host.toggle_flag_clear(flag);
    }

    // Sort, dedup, derive the weapon index and the ammo total; the slot count
    // comes back with the game's own floor applied.
    (void)host.inventory_sort();

    if (const std::int32_t equipped = host.item_number(params.equipped); equipped >= 0) {
        host.weapon_attack_set(host.weapon_attack(equipped));
    }
    const std::int32_t slot = host.weapon_inventory_index();
    if (slot != -1) {
        // The starting weapon must not read as empty: seed the clip from its
        // own stack or the next ammo write-back stores the zero over it.
        host.current_weapon_ammo_set(host.item_count_at_slot(slot));
    }
}

}  // namespace sh::gameflow
