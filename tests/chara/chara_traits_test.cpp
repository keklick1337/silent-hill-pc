// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The per-type traits of gamedata/chara/<Name>/character.json against a golden table of the original's rules by
// character number: the blood kind and its hit rules, the single-table types, the shared entities, the pooled and
// the spawnable range, the effects' Split Head / Floatstinger / Twinfeeler checks and the quick save boss list.

#include "framework/sh_test.h"

#include "engine/chara/chara_library.h"
#include "engine/game_state/game_ids.h"
#include "engine/gconfig/gameplay_params.h"
#include "engine/ids/ids.h"
#include "engine/items/item_library.h"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ids = sh::game_state::chara;
using sh::chara::CharacterDef;
using sh::chara::CharaLibrary;

namespace {

fs::path repo_gamedata() { return fs::path(SH_PROJECT_ROOT) / "gamedata"; }

// attack.cpp blood_kind, with the hit's Harry case (kind 1 while his health is not below 0).
int golden_blood_kind(int id) {
    switch (id) {
        case ids::kHarry: return 1;
        case ids::kAirScreamer:
        case ids::kNightFlutter: return 2;
        case ids::kStalker: return 4;
        case ids::kLarvalStalker:
        case ids::kGreyChild:
        case ids::kMumbler: return 3;
        case ids::kCreeper: return 5;
        case ids::kGroaner:
        case ids::kWormhead: return 6;
        case ids::kSplitHead: return 7;
        case ids::kHangedScratcher: return 8;
        case ids::kFloatstinger:
        case ids::kTwinfeeler: return 10;
        case ids::kRomper: return 9;
        case ids::kPuppetNurse:
        case ids::kPuppetDoctor: return 11;
        case ids::kBloodsucker: return 12;
        case ids::kPadlock: return -1;
        default: return 16;
    }
}

bool golden_fresh_hit(int id) { return id == ids::kIncubus || id == ids::kUnknown23 || id == ids::kIncubator; }

// combat_host.cpp single_table_type.
bool golden_single_table(int id) {
    return id == ids::kBloodsucker || id == ids::kCreeper || id == ids::kLarvalStalker || id == ids::kGroaner ||
           id == ids::kWormhead || id == ids::kRomper;
}

// entity_host.cpp shares: the type whose state.json a type without one uses (0: none).
int golden_shares(int id) {
    switch (id) {
        case ids::kNightFlutter: return ids::kAirScreamer;
        case ids::kWormhead: return ids::kGroaner;
        case ids::kGreyChild:
        case ids::kMumbler: return ids::kStalker;
        case ids::kDummyNurse:
        case ids::kPuppetDoctor:
        case ids::kDummyDoctor: return ids::kPuppetNurse;
        case ids::kUnknown23: return ids::kIncubus;
        default: return 0;
    }
}

// quick save's boss list.
bool golden_boss(int id) {
    return id == ids::kSplitHead || id == ids::kFloatstinger || id == ids::kTwinfeeler || id == ids::kBloodsucker ||
           id == ids::kIncubus || id == ids::kMonsterCybil || id == ids::kIncubator;
}

// particle_host.cpp strike_player: rows 60 and 62 of the attack table (attack_table_test checks the names' rows).
std::string golden_acid_attack(int id) {
    return id == ids::kFloatstinger ? "FloatstingerAcid" : id == ids::kTwinfeeler ? "TwinfeelerAcid" : "";
}

}  // namespace

SH_TEST(chara_traits_match_the_old_switches) {
    std::vector<std::string> warnings;
    const auto loaded = CharaLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(loaded);
    SH_CHECK(warnings.empty());
    const CharaLibrary& lib = loaded.value();
    for (int id = 0; id < ids::kCount; ++id) {
        const CharacterDef* d = lib.find(static_cast<std::uint8_t>(id));
        SH_CHECK(d != nullptr);
        if (d == nullptr) {
            continue;
        }
        SH_CHECK(d->blood_kind == golden_blood_kind(id));
        SH_CHECK(d->blood_at_zero_health == (id == ids::kHarry));
        SH_CHECK(d->blood_strength == (id == ids::kBloodsucker ? 1 : -1));
        SH_CHECK(d->fresh_hit_blood_strength == (golden_fresh_hit(id) ? 7 : -1));
        SH_CHECK(d->fresh_hit_blood_strength_scatter == (golden_fresh_hit(id) ? 4 : -1));
        SH_CHECK(d->blood_follows_body == (id != ids::kSplitHead));
        SH_CHECK(d->blood_depth_bias == (id == ids::kFloatstinger ? 96 : 32));
        SH_CHECK(d->impact_sound == (id == ids::kPadlock ? "melee_impact_thrown" : ""));
        SH_CHECK(d->is_player == (id == ids::kHarry));
        // attack.cpp: AirScreamer <= id < LockerDeadBody (the original's AirScreamer..CHARA_LAST_ENEMY_ID).
        SH_CHECK(d->enemy == (id >= ids::kAirScreamer && id < ids::kLockerDeadBody));
        SH_CHECK(d->single_clip_table == golden_single_table(id));
        // chara_pool.cpp / chara_spawn.cpp: AirScreamer..Parasite, but the two placeholder types the original never
        // spawns (their only model is the one-triangle CHARA/DUMMY.ILM): "pool": false.
        const bool placeholder = id == ids::kDummyNurse || id == ids::kDummyDoctor;
        SH_CHECK(d->pooled() == (id >= ids::kAirScreamer && id <= ids::kParasite && !placeholder));
        SH_CHECK(d->is_boss == golden_boss(id));
        SH_CHECK(d->acid_attack_name == golden_acid_attack(id));
        SH_CHECK(d->acid_attack == 0);
        SH_CHECK(d->pool_model.empty());
        SH_CHECK(d->pool_texture.empty());
        SH_CHECK(d->pool_anim.empty());

        // The state layout: a type without its own state.json uses its layout folder's, as `shares` said.
        const bool own = fs::is_regular_file(repo_gamedata() / "chara" / d->folder / "state.json");
        if (!own) {
            const std::string& folder = d->state_layout_folder();
            const bool has_file =
                !folder.empty() && fs::is_regular_file(repo_gamedata() / "chara" / folder / "state.json");
            const int shared = has_file ? lib.find_id(folder) : 0;
            SH_CHECK(shared == golden_shares(id));
        }
    }
}

// gameplay_config.cpp's heal switch (HealthDrink 40, FirstAidKit 80, Ampoule 100) and pc_glue_host's saturation item
// (the Ampoule) as item data: "heal" names the gameplay.json row.
SH_TEST(item_heal_rows_match_the_old_switch) {
    std::vector<std::string> warnings;
    const auto items = sh::items::ItemLibrary::load(repo_gamedata(), warnings);
    SH_CHECK(items);
    const auto gameplay = sh::gconfig::GameplayLibrary::load(repo_gamedata(), {}, warnings);
    SH_CHECK(gameplay);
    if (!items || !gameplay) {
        return;
    }
    const auto& amounts = gameplay.value().params().heal_amounts;
    int healing = 0;
    for (const sh::items::ItemDef& item : items.value().items()) {
        const std::string& row = item.inventory.heal;
        const auto local = sh::ids::local_name(item.id);
        double expected = 0.0;
        if (local == "HealthDrink") expected = 40.0;
        if (local == "FirstAidKit") expected = 80.0;
        if (local == "Ampoule") expected = 100.0;
        const auto it = amounts.find(row);
        const double amount = row.empty() || it == amounts.end() ? 0.0 : it->second;
        SH_CHECK(amount == expected);
        SH_CHECK(item.inventory.fills_saturation == (local == "Ampoule"));
        healing += row.empty() ? 0 : 1;
    }
    SH_CHECK(healing == 3);
}

// A character the data says nothing about gets the defaults; null puts a trait back.
SH_TEST(chara_traits_defaults_and_reset) {
    std::vector<std::string> warnings;
    auto parsed = CharaLibrary::parse(R"json({"characters": [{"id": 100, "enum": "Chara_ModBeast",
        "blood": "Groaner", "enemy": true, "pool": false, "spawn": {"kind": "enemy"}}]})json", warnings);
    SH_CHECK(parsed);
    CharaLibrary& lib = parsed.value();
    lib.resolve_blood(R"json({"kinds": {"Groaner": {"kind": 6}, "Default": {"kind": 16}}})json", "blood.json",
                      warnings);
    const CharacterDef* d = lib.find(100);
    SH_CHECK(d != nullptr);
    SH_CHECK(d->blood_kind == 6);
    SH_CHECK(d->enemy);
    SH_CHECK(!d->pooled());  // "pool": false wins over the spawn block
    SH_CHECK(!d->is_player && !d->is_boss && d->blood_follows_body && d->blood_depth_bias == 32);
    SH_CHECK(lib.apply_patch(R"json({"blood": null, "pool": null, "enemy": null})json", "patch",
                             "silenthill:ModBeast"));
    lib.resolve_blood(R"json({"kinds": {"Default": {"kind": 16}}})json", "blood.json", warnings);
    d = lib.find(100);
    SH_CHECK(d->blood == "Default" && d->blood_kind == 16);
    SH_CHECK(!d->enemy);
    SH_CHECK(d->pooled());  // back to "has a spawn block"
    std::vector<std::string> unknown;
    SH_CHECK(lib.apply_patch(R"json({"blood": "Purple"})json", "patch", "silenthill:ModBeast"));
    lib.resolve_blood(R"json({"kinds": {"Default": {"kind": 16}}})json", "blood.json", unknown);
    SH_CHECK(lib.find(100)->blood_kind == 16);
    SH_CHECK(!unknown.empty());
}
