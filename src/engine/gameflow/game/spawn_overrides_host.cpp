// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The story-driven spawns of the running map: the flag writes and the spawn table variants
// chosen from gamedata/maps/<map>/ (sh_maps) and written into sh::maps::map_runtime()'s spawn table, the save's
// killed-enemy words and the event flags. The map data itself is read by the map load
// (engine/loader/active_map.cpp).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/game_state/game_state.h"
#include "engine/game_state/save_record.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_runtime.h"
#include "engine/maps/sh_maps.h"

extern "C" {
void Chara_SpawnFlagsSet(std::int32_t charaId, std::int32_t spawnIdx, std::int32_t spawnFlags);  // npc_spawn_host.cpp
void MapData_SpawnOverridesApply(void);
void MapData_SpawnVariantsApply(void);
void MapData_SpawnVariantsStateSave(void);
}

namespace {

constexpr int kWritesMax = 64;

// True the first time `name` is seen: the selector runs every frame (killed-state write-back), so an unknown flag
// name is reported once, not per frame. Past the first 32 names every name counts as new.
bool unknown_flag_first_seen(const char* name) {
    static std::vector<std::string> seen;
    for (const std::string& s : seen) {
        if (std::strncmp(s.c_str(), name, 63) == 0) {
            return false;
        }
    }
    if (seen.size() < 32) {
        seen.emplace_back(std::string(name).substr(0, 63));
    }
    return true;
}

// An event flag by name; an unknown name reads as clear with a warning: a mod with a typo must not abort the map's
// data load.
int spawn_flag_get(void*, const char* name) {
    const int number = sh::ids::flag_number(name);
    if (number < 0) {
        if (unknown_flag_first_seen(name)) {
            SH_WARN("[SPAWNOVR] unknown event flag \"%s\"; the condition reads it as clear", name);
        }
        return 0;
    }
    return sh::game_state::event_flag(number) ? 1 : 0;
}

// The spawn flag bits by name: "none" and the five bits.
int spawn_flag_bits(const char* name) {
    static const char* const kNames[6] = {"none", "spawn_0", "spawn_1", "spawn_2", "spawn_3", "spawn_4"};
    for (int i = 0; i < 6; ++i) {
        if (std::strcmp(name, kNames[i]) == 0) {
            return i == 0 ? 0 : 1 << (i - 1);
        }
    }
    return -1;
}

const char* map_name() { return sh::maps::map_runtime().name().c_str(); }

// The selector's choice over the map's spawn_variants; its flags are set as part of choosing (the original sets its
// default variant's flag while it decides). False: no variants or no match.
bool spawn_variant_pick(ShMapSpawnVariantPick* pick) {
    if (!sh::game_state::save_active() ||
        !ShMaps_SpawnVariantSelect(sh::game_state::save_difficulty(), spawn_flag_get, nullptr, pick)) {
        return false;
    }
    for (int f = 0; f < pick->setFlagCount; ++f) {
        const int number = sh::ids::flag_number(pick->setFlags[f]);
        if (number < 0) {
            if (unknown_flag_first_seen(pick->setFlags[f])) {
                SH_WARN("[SPAWNVAR] %s: unknown event flag \"%s\"; not set", map_name(), pick->setFlags[f]);
            }
            continue;
        }
        sh::game_state::set_event_flag(number, true);
    }
    return true;
}

}  // namespace

// Story-driven spawns (maps/<map>/spawn_overrides.json, map.json "spawn_overrides"): the generic selector, written
// through Chara_SpawnFlagsSet in file order. It runs from the world-object init of the map, where the original's
// per-map blocks ran: at map load the spawn table and the animation slots still belong to the previous map.
extern "C" void MapData_SpawnOverridesApply(void) {
    const int difficulty = sh::game_state::save_active() ? sh::game_state::save_difficulty() : 0;
    ShMapSpawnWrite writes[kWritesMax];

    // A variant replaces the whole table first; the flag writes then act on the chosen rows.
    MapData_SpawnVariantsApply();

    const int selected = ShMaps_SpawnOverridesSelect(difficulty, spawn_flag_get, nullptr, writes, kWritesMax);
    if (selected <= 0) {
        return;
    }
    if (selected > kWritesMax) {
        SH_WARN("[SPAWNOVR] %s: %d writes selected, applying the first %d", map_name(), selected, kWritesMax);
    }
    int applied = 0;
    for (int i = 0; i < selected && i < kWritesMax; ++i) {
        const int chara = ShMaps_CharaNumberOf(writes[i].chara);
        if (chara < 0) {
            SH_WARN("[SPAWNOVR] %s: unknown character \"%s\"; entry skipped", map_name(), writes[i].chara);
            continue;
        }
        int flags = 0;
        bool ok = true;
        for (int f = 0; f < writes[i].flagCount; ++f) {
            const int bits = spawn_flag_bits(writes[i].flags[f]);
            if (bits < 0) {
                SH_WARN("[SPAWNOVR] %s: unknown spawn flag \"%s\"; entry skipped", map_name(), writes[i].flags[f]);
                ok = false;
                break;
            }
            flags |= bits;
        }
        if (!ok) {
            continue;
        }
        Chara_SpawnFlagsSet(chara, writes[i].slot, flags);
        ++applied;
    }
    SH_LOG("[SPAWNOVR] %s: %d spawn flag writes applied", map_name(), applied);
}

// Copies the chosen variant over the map's spawn table and loads its killed-enemy word into the map's own word.
// Runs at world-object init (from MapData_SpawnOverridesApply) and whenever a script changes the flags that pick the
// variant.
extern "C" void MapData_SpawnVariantsApply(void) {
    ShMapSpawnVariantPick pick;
    if (!spawn_variant_pick(&pick)) {
        return;
    }
    for (int i = 0; i < SH_MAP_SPAWN_GROUPS * SH_MAP_SPAWN_SLOTS; ++i) {
        sh::maps::map_runtime().set_spawn(i, pick.rows[i]);
    }
    std::int32_t* words = sh::game::save_record().enemy_states;
    words[pick.enemyStateActive] = words[pick.enemyStateLoad];
    SH_DBG("[SPAWNVAR] %s: variant %s (killed state %d -> %d)", map_name(), pick.variant, pick.enemyStateLoad,
           pick.enemyStateActive);
}

// Every frame while the map runs: the map's own killed-enemy word goes back to the chosen variant's word, so a
// variant switch or the next load finds the kills made under it.
extern "C" void MapData_SpawnVariantsStateSave(void) {
    ShMapSpawnVariantPick pick;
    if (!spawn_variant_pick(&pick)) {
        return;
    }
    std::int32_t* words = sh::game::save_record().enemy_states;
    words[pick.enemyStateSave] = words[pick.enemyStateActive];
}
