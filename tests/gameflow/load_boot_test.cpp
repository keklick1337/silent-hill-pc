// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "framework/sh_test.h"

#include "engine/gameflow/load_boot.h"

#include <cstring>
#include <string>
#include <vector>

using namespace sh::gameflow;

namespace {

struct SaveRecorder final : SaveInitHost {
    std::vector<std::string> log;
    std::int32_t map = 0, difficulty = 99, paper = 0, enemy_value = 0;
    int enemy_writes = 0;
    int lowest_index = 1000;

    void savegame_clear() override { log.push_back("clear"); }
    void map_index_set(std::int32_t id) override { map = id; log.push_back("map"); }
    void difficulty_set(std::int32_t d) override { difficulty = d; log.push_back("difficulty"); }
    void paper_map_set(std::int32_t page) override { paper = page; log.push_back("paper"); }
    void enemy_state_set(std::int32_t index, std::int32_t value) override {
        enemy_writes++;
        enemy_value = value;
        if (index < lowest_index) lowest_index = index;
    }
    void player_reset() override { log.push_back("player"); }
};

struct EntryRecorder final : MapEntryHost {
    int force = 0, demo = 0, newgame = 0, empty = 0;
    int map_idx = 0;
    std::vector<std::string> log;
    struct Slot { int item, count; };
    std::vector<Slot> slots;
    int equipped = -1;
    unsigned toggles = 0;
    int sorted = 0;
    int attack_set = -1;
    int weapon_idx = -1;   // NO_VALUE unless the sort finds a weapon
    int ammo_set = -1;

    int force_enemy_spawn() const override { return force; }
    int boot_demo() const override { return demo; }
    int new_game() const override { return newgame; }
    int inventory_empty() const override { return empty; }
    const char* map_name(std::int32_t idx) const override {
        static const char* kNames[] = {"map0_s00", "map0_s01", "map1_s00", "map7_s03"};
        return (idx >= 0 && idx <= 3) ? kNames[idx] : "mapX";
    }
    void no_spawn_flag_clear() override { log.push_back("no_spawn_clear"); }
    std::int32_t item_number(std::string_view name) const override {
        if (name == "Flashlight") return 224;
        if (name == "PocketRadio") return 225;
        if (name == "KitchenKnife") return 128;
        if (name == "Handgun") return 160;
        if (name == "HandgunBullets") return 192;
        return -1;
    }
    std::int32_t toggle_number(std::string_view name) const override {
        if (name == "radio_on") return 1;
        if (name == "flashlight_off") return 2;
        return -1;
    }
    void item_slot_set(std::int32_t slot, std::int32_t item, std::int32_t count) override {
        while ((int)slots.size() <= slot) slots.push_back({-1, 0});
        slots[slot] = {item, count};
    }
    void equipped_set(std::int32_t item) override { equipped = item; }
    void toggle_flag_set(std::int32_t flag) override { toggles |= (unsigned)flag; }
    void toggle_flag_clear(std::int32_t flag) override { toggles &= ~(unsigned)flag; }
    std::int32_t inventory_sort() override {
        sorted++;
        weapon_idx = (equipped == 160) ? 4 : -1;  // the sort derives the weapon slot
        return 8;
    }
    std::int32_t weapon_attack(std::int32_t item) const override { return item - 128; }
    void weapon_attack_set(std::int32_t attack) override { attack_set = attack; }
    std::int32_t weapon_inventory_index() const override { return weapon_idx; }
    std::int32_t item_count_at_slot(std::int32_t slot) const override {
        return slot < (int)slots.size() ? slots[slot].count : 0;
    }
    void current_weapon_ammo_set(std::int32_t count) override { ammo_set = count; }
};

}  // namespace

SH_TEST(savegame_initialize_order_and_clamp) {
    SaveRecorder h;
    savegame_initialize(h, 7, 5, SaveInitParams{});  // difficulty above the table
    // clear, map, difficulty, paper, 45 enemy writes, player reset - in order.
    SH_CHECK(h.log.size() == 5);
    SH_CHECK(h.log[0] == "clear" && h.log[1] == "map" && h.log[2] == "difficulty" &&
             h.log[3] == "paper" && h.log[4] == "player");
    SH_CHECK(h.map == 7);
    SH_CHECK(h.difficulty == 1);   // clamped to hard
    SH_CHECK(h.paper == 1);
    SH_CHECK(h.enemy_writes == 45 && h.enemy_value == -1 && h.lowest_index == 0);

    SaveRecorder h2;
    savegame_initialize(h2, 0, -5, SaveInitParams{});
    SH_CHECK(h2.difficulty == -1);  // clamped to easy

    SaveRecorder h3;
    SaveInitParams p;  // the clamp bounds are data
    p.difficulty_min = 0;
    p.difficulty_max = 0;
    savegame_initialize(h3, 1, 3, p);
    SH_CHECK(h3.difficulty == 0);

    SaveInitParams many;
    many.enemy_state_count = 3;
    SaveRecorder h4;
    savegame_initialize(h4, 1, 0, many);
    SH_CHECK(h4.enemy_writes == 3 && h4.lowest_index == 0);
}

SH_TEST(map_entry_spawn_gate_conditions) {
    const MapEntryParams p = MapEntryParams::builtin();

    EntryRecorder on;
    on.force = 1;  // non-tutorial map, no demo: clears
    map_entry_spawn_gate(on, 2, p);
    SH_CHECK(on.log.size() == 1 && on.log[0] == "no_spawn_clear");

    EntryRecorder tutorial;  // map0_s00 and map0_s01 never clear
    tutorial.force = 1;
    map_entry_spawn_gate(tutorial, 0, p);
    map_entry_spawn_gate(tutorial, 1, p);
    SH_CHECK(tutorial.log.empty());

    EntryRecorder demo;  // the attract demo installs its own enemy state
    demo.force = 1;
    demo.demo = 1;
    map_entry_spawn_gate(demo, 2, p);
    SH_CHECK(demo.log.empty());

    EntryRecorder off;  // gameplay change off by default
    map_entry_spawn_gate(off, 2, p);
    SH_CHECK(off.log.empty());
}

SH_TEST(map_entry_start_loadout_gates_and_order) {
    const MapEntryParams p = MapEntryParams::builtin();

    EntryRecorder fire;
    fire.newgame = 1;
    fire.empty = 1;
    map_entry_start_loadout(fire, 2, p);
    // The five loadout items in slots 0..4.
    SH_CHECK(fire.slots.size() == 5);
    SH_CHECK(fire.slots[0].item == 224 && fire.slots[0].count == 1);    // Flashlight
    SH_CHECK(fire.slots[1].item == 225 && fire.slots[1].count == 1);    // PocketRadio
    SH_CHECK(fire.slots[2].item == 128 && fire.slots[2].count == 1);    // KitchenKnife
    SH_CHECK(fire.slots[3].item == 160 && fire.slots[3].count == 1);    // Handgun
    SH_CHECK(fire.slots[4].item == 192 && fire.slots[4].count == 15);   // HandgunBullets
    SH_CHECK(fire.equipped == 160);
    SH_CHECK(fire.toggles == 1);  // radio on, flashlight-off bit cleared
    SH_CHECK(fire.sorted == 1);   // exactly one recount, after the items
    SH_CHECK(fire.attack_set == 32);        // the handgun's tap attack
    SH_CHECK(fire.ammo_set == 15);          // seeded from its own stack

    // Gates: tutorial map, not a New Game, or a non-empty inventory - no fire.
    EntryRecorder tut, notNew, notEmpty;
    tut.newgame = tut.empty = 1;
    notNew.empty = 1;
    notEmpty.newgame = 1;
    map_entry_start_loadout(tut, 0, p);
    map_entry_start_loadout(notNew, 2, p);
    map_entry_start_loadout(notEmpty, 2, p);
    SH_CHECK(tut.slots.empty() && tut.sorted == 0);
    SH_CHECK(notNew.slots.empty() && notNew.sorted == 0);
    SH_CHECK(notEmpty.slots.empty() && notEmpty.sorted == 0);
}

SH_TEST(map_entry_params_parse) {
    const char* kJson = R"json({
        "map_entry": {
            "tutorial_maps": ["map0_s00"],
            "start_loadout": {
                "items": [ { "item": "Handgun", "count": 1 }, { "item": "HandgunBullets", "count": 30 } ],
                "equipped": "Handgun",
                "toggle_set": "radio_on",
                "toggle_clear": "flashlight_off"
            }
        }
    })json";
    const auto parsed = MapEntryParams::parse(kJson);
    SH_CHECK(parsed);
    const MapEntryParams& p = parsed.value();
    SH_CHECK(p.tutorial_maps.size() == 1 && p.tutorial_maps[0] == "map0_s00");
    SH_CHECK(p.items.size() == 2);
    SH_CHECK(p.items[0].item == "Handgun" && p.items[0].count == 1);
    SH_CHECK(p.items[1].item == "HandgunBullets" && p.items[1].count == 30);
    SH_CHECK(p.equipped == "Handgun");
    SH_CHECK(p.toggle_set == "radio_on" && p.toggle_clear == "flashlight_off");

    // Missing sections keep the built-ins; bad JSON fails.
    SH_CHECK(MapEntryParams::parse("{}"));
    SH_CHECK(MapEntryParams::parse("{}").value().items.size() == 5);
    SH_CHECK(!MapEntryParams::parse("{"));
    SH_CHECK(!SaveInitParams::parse("{"));
}
