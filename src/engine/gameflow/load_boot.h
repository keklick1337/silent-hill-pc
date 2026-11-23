#pragma once

// The savegame initialization and the map-entry decisions of the boot:
// what a fresh savegame
// starts with, the force-enemy-spawn gate and the starting loadout a New Game
// that skipped the intro gets. The machine owns the order, the gates and the
// arithmetic; the records, the item/flag numbers behind the names and the
// systems are reached through the hosts. Numbers and names live in
// gamedata/config/load_boot.json (a mod replaces the file; missing or bad file
// keeps the built-in values, which are the original's).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

// ---- savegame_initialize ---------------------------------------------------------

struct SaveInitParams {
    std::int32_t difficulty_min = -1;      // easy
    std::int32_t difficulty_max = 1;       // hard
    std::int32_t enemy_state_count = 45;   // every overlay enemy slot starts "alive"
    std::int32_t enemy_state_value = -1;   // NO_VALUE
    std::int32_t start_paper_map = 1;      // the Old Town page

    static SaveInitParams builtin() { return {}; }
    static Result<SaveInitParams> parse(std::string_view json);
};

class SaveInitHost {
public:
    virtual ~SaveInitHost() = default;

    virtual void savegame_clear() = 0;                 // zero the whole record
    virtual void map_index_set(std::int32_t overlay_id) = 0;
    virtual void difficulty_set(std::int32_t difficulty) = 0;
    virtual void paper_map_set(std::int32_t page) = 0;
    virtual void enemy_state_set(std::int32_t index, std::int32_t value) = 0;
    virtual void player_reset() = 0;                   // the player's own defaults
};

// The original of GameBoot_SavegameInitialize: clear, seed, mark every enemy
// state alive, reset the player. The difficulty is clamped to the table's
// bounds before it is stored.
void savegame_initialize(SaveInitHost& host, std::int32_t overlay_id, std::int32_t difficulty,
                         const SaveInitParams& params);

// ---- map_entry gates ---------------------------------------------------------------

struct StartLoadoutEntry {
    std::string item;   // item name, the host numbers it
    std::int32_t count = 1;
};

struct MapEntryParams {
    std::vector<std::string> tutorial_maps;  // maps the gates never touch ("map0_s00", ...)
    std::vector<StartLoadoutEntry> items;    // written to slots 0.. in order
    std::string equipped;                    // weapon equipped with the loadout
    std::string toggle_set;                  // item toggle flag turned on ("radio_on")
    std::string toggle_clear;                // item toggle flag turned off ("flashlight_off")

    static MapEntryParams builtin();
    static Result<MapEntryParams> parse(std::string_view json);
};

class MapEntryHost {
public:
    virtual ~MapEntryHost() = default;

    // Gates.
    // int, not bool: a host defined inside the game's C ABI region cannot spell bool
    virtual int force_enemy_spawn() const = 0;   // config gameplay change, off by default
    // an attract demo is installing its own state
    virtual int boot_demo() const { return sh::game::session().process_flag(sh::game::process_flag::kBootDemo) ? 1 : 0; }
    virtual int new_game() const { return sh::game::session().process_flag(sh::game::process_flag::kNewGame) ? 1 : 0; }
    virtual int inventory_empty() const = 0;     // every slot holds the empty marker
    virtual const char* map_name(std::int32_t map_idx) const = 0;

    // Records and systems.
    virtual void no_spawn_flag_clear() { sh::game::session().set_sys_flag(sh::game::sys_flag::kNoEnemySpawn, false); }
    virtual std::int32_t item_number(std::string_view name) const = 0;   // -1 when unknown
    virtual std::int32_t toggle_number(std::string_view name) const = 0; // -1 when unknown
    virtual void item_slot_set(std::int32_t slot, std::int32_t item, std::int32_t count) = 0;
    virtual void equipped_set(std::int32_t item) = 0;
    virtual void toggle_flag_set(std::int32_t flag) = 0;
    virtual void toggle_flag_clear(std::int32_t flag) = 0;
    virtual std::int32_t inventory_sort() = 0;               // sorts; answers the live slot count
    virtual std::int32_t weapon_attack(std::int32_t item) const = 0;  // the equipped weapon's attack
    virtual void weapon_attack_set(std::int32_t attack) = 0;
    virtual std::int32_t weapon_inventory_index() const = 0; // NO_VALUE when no weapon is held
    virtual std::int32_t item_count_at_slot(std::int32_t slot) const = 0;
    virtual void current_weapon_ammo_set(std::int32_t count) = 0;
};

// The force-enemy-spawn gate of GameBoot_MapLoad: on a non-tutorial map, with
// the gameplay change on and no demo installing its own enemy state, clear the
// no-spawn flag.
void map_entry_spawn_gate(MapEntryHost& host, std::int32_t map_idx, const MapEntryParams& params);

// The starting loadout of GameBoot_MapLoad: a New Game that booted straight
// onto a config-selected map never ran the intro that hands Harry his gear, so
// an EMPTY inventory gets the loadout (anything else is live state — the gate
// must not fire on saves or mid-playthrough unequips). The sort recount and the
// ammo write-back are part of the order.
void map_entry_start_loadout(MapEntryHost& host, std::int32_t map_idx, const MapEntryParams& params);

}  // namespace sh::gameflow
