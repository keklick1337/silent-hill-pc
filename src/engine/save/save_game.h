// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// The engine's save game: what a save holds, by name. Items, event flags, maps,
// paper maps and save points are readable ids ("silenthill:Handgun", "map1_s02", "Cafe"); a number appears only for
// a thing this build has no name for. Written as the STAT chunk, JSON "sh-savegame/1".
//
// The running game keeps its progress and settings in sh::game::SaveRecord and sh::game::Settings by name;
// from_live_* / to_live_* convert them to and from SaveGame field by field. The console's byte form of the same
// records (the GAME and OPTS chunks of format 1 files, the PS1 memory cards) is read through console_records.h.

#include "engine/game_state/progress_record.h"
#include "engine/game_state/settings_record.h"
#include "formats/common/status.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::save {

inline constexpr const char* kSaveGameFormat = "sh-savegame/1";
// "flag_names" of the STAT JSON: 2 = the flag names of gamedata/flags/. A save without the key
// was written with the older names: a name that has moved to another flag since is read as the flag it meant.
inline constexpr int kFlagNamesVersion = 2;

// A thing of the save: its name, or its number when this build has no name for it (a mod's item whose data is gone,
// a flag bit nobody named). Exactly one of the two is used.
struct Ref {
    std::string name;
    std::int64_t number = 0;

    bool named() const { return !name.empty(); }
    static Ref of_name(std::string text) { return Ref{std::move(text), 0}; }
    static Ref of_number(std::int64_t n) { return Ref{{}, n}; }
    bool operator==(const Ref&) const = default;
};

// Name <-> number for each kind of thing (installed by the game; empty functions: no names, every
// thing stays a number). number_of returns -1 for a name it does not know.
struct NameTable {
    std::function<const char*(int)> item_name;
    std::function<int(const char*)> item_number;
    std::function<const char*(int)> flag_name;
    std::function<int(const char*)> flag_number;
    std::function<const char*(int)> map_name;
    std::function<int(const char*)> map_number;
    std::function<const char*(int)> paper_map_name;
    std::function<int(const char*)> paper_map_number;
    // The flag a name meant in a save written before the name moved to another flag (nullptr: the name means
    // itself). Saves without "flag_names" (kFlagNamesVersion) and the NAME chunk of format 1 files are such saves.
    std::function<const char*(const char*)> flag_name_in_old_save;
};

struct InventoryEntry {
    int slot = 0;
    std::optional<Ref> item;  // empty: the slot is empty (its other bytes may still be set)
    std::int32_t count = 0;
    std::uint8_t command = 0;  // the inventory command of the item (equip, use, ...)
    std::uint8_t extra = 0;    // the slot's fourth byte (the game sorts with it)
    bool operator==(const InventoryEntry&) const = default;
};

struct EnemyState {
    Ref map;
    std::int32_t alive_bits = -1;  // the map's word of enemies still alive; -1 (all) is the default and not listed
    bool operator==(const EnemyState&) const = default;
};

// The controller binds and the option screen of the game (pad bits of each action).
struct GameOptions {
    std::uint16_t bind_enter = 0, bind_cancel = 0, bind_skip = 0, bind_action = 0, bind_aim = 0, bind_light = 0,
                  bind_run = 0, bind_view = 0, bind_step_left = 0, bind_step_right = 0, bind_pause = 0, bind_item = 0,
                  bind_map = 0, bind_option = 0;
    std::int8_t screen_x = 0, screen_y = 0;
    std::uint8_t mono = 0, volume_bgm = 0, volume_se = 0, vibration = 0, brightness = 0, weapon_control = 0,
                 blood_color = 0;
    std::int8_t auto_load = 0;
    std::uint8_t extra_options = 0;  // unlocked extra options (bits)
    std::int8_t view_control = 0, view_mode = 0, retreat_turn = 0, walk_run_control = 0, auto_aiming = 0,
                bullet_adjust = 0;
    std::uint16_t seen_game_over_tips = 0;
    std::uint32_t pal_language = 0;
    bool operator==(const GameOptions&) const = default;
};

struct SaveGame {
    // Where.
    Ref map;
    int room = 0;
    Ref location;   // save point name ("Cafe"); see location_names()
    Ref paper_map;  // the paper map the map screen opens
    int difficulty = 0;  // -1 easy, 0 normal, 1 hard (other values kept as numbers)

    // Harry.
    std::int32_t health = 0;             // Q12
    std::int32_t health_saturation = 0;  // Q12, stored health of ampoules
    std::int32_t position_x = 0, position_z = 0;  // Q12 metres
    std::int16_t rotation_y = 0;         // Q12 turns
    std::int8_t player_reset_byte = 0;   // cleared with the player's reset, otherwise unused

    // Inventory.
    int slot_count = 0;
    Ref equipped;  // the weapon in hand; number 0: none
    std::vector<InventoryEntry> items;  // slots that are not blank (item 0xFF, count 0, command 0, extra 0xFF), ascending
    std::uint32_t item_toggle_flags = 0;
    std::uint8_t inventory_item_flags = 0;

    // World.
    std::vector<std::string> flags;       // set event flags with a name, in bit order
    std::vector<int> flag_bits;           // set event flag bits without a name
    bool old_flag_names = false;          // `flags` holds the names of before kFlagNamesVersion (a read save)
    std::vector<std::string> paper_maps;  // paper maps held
    std::vector<int> paper_map_bits;      // held bits without a name
    std::vector<EnemyState> enemy_states;  // maps whose word is not -1, ascending map number

    // Progress and the results screen.
    std::uint32_t play_time = 0;  // Q12 seconds
    std::int32_t run_distance = 0, walk_distance = 0;  // Q12
    std::int16_t save_count = 0;
    std::int16_t picked_up_items = 0;
    std::uint8_t special_items = 0;  // 0..31
    bool next_fear = false;
    std::uint8_t extra_hours = 0;    // 0..3, 290 h each
    std::uint8_t clear_count = 0;
    std::uint8_t endings = 0;        // endings ever reached (bits)
    std::int8_t current_ending_flags = 0;
    std::uint8_t continue_count = 0;
    std::uint8_t melee_kills = 0, melee_kills_b = 0, ranged_kills = 0;
    std::uint32_t hit_distance_total = 0;  // 28 bits: distance of ranged hits, summed (results ranking)
    std::uint16_t shots_fired = 0, close_hits = 0, mid_hits = 0, long_hits = 0;
    // Further counters of the ranking code, named by what increments them.
    std::uint16_t far_hits = 0, mid_kill_hits = 0, long_kill_hits = 0, far_kill_hits = 0, melee_hits = 0,
                  melee_kill_hits = 0, other_attacks = 0;

    std::optional<GameOptions> options;  // the options when the game saved; none: loading keeps the current ones

    bool operator==(const SaveGame&) const = default;
};

// The save point names, by the game's location number (0 "Anywhere" .. 24 "NextFear").
std::span<const char* const> location_names();
// "Room 302" for "Room302": the English words the save list shows (ui.save.location.*); nullptr when unknown.
const char* location_display_name(int location);
// Number of a location name, -1 when unknown.
int location_number(std::string_view name);

// JSON (pretty, stable order: the same game gives the same text).
std::string to_json(const SaveGame& game);
Result<SaveGame> from_json(std::string_view text);

// The old record layout (see the top of the file).
namespace layout {
inline constexpr std::size_t kStateSize = 636;     // the game's save record
inline constexpr std::size_t kStateRecord = 640;   // with its footer: the GAME chunk of format 1, a card save
inline constexpr std::size_t kOptionsSize = 56;    // the game's options record
inline constexpr std::size_t kOptionsRecord = 128;  // padded, with its footer: the OPTS chunk, a card file's options
inline constexpr int kItemSlots = 40;
inline constexpr int kFlagWords = 52;
inline constexpr int kEnemyStateWords = 45;
}  // namespace layout

// Problems of a conversion that do not stop it (a name this build does not know); for the journal.
using Warnings = std::vector<std::string>;

// The game's live progress record -> SaveGame.
SaveGame from_live_state(const game::SaveRecord& state, const NameTable& names);
// SaveGame -> the live progress record. Names this build does not know: the item slot is left blank, the flag is not
// set, a paper map not held; each one is a warning. Fails only when the map has no number here.
Result<game::SaveRecord> to_live_state(const SaveGame& game, const NameTable& names, Warnings& warnings);

GameOptions from_live_options(const game::Settings& options);
game::Settings to_live_options(const GameOptions& options);

// A format 1 save (or a card's): the GAME record (kStateRecord, footer checked as the game did), the optional NAME
// chunk of format 1 (names win over the record's numbers, as they did) and the optional OPTS record (used when its
// footer is valid). Fails on a short record or a bad footer: the game showed such a save as damaged.
Result<SaveGame> from_legacy(std::span<const std::uint8_t> game_record, std::string_view names_chunk,
                             std::span<const std::uint8_t> options_record, const NameTable& names, Warnings& warnings);
// The old GAME record of a save, sealed (tests, tools): what format 1 stored.
Result<std::vector<std::uint8_t>> to_legacy_record(const SaveGame& game, const NameTable& names, Warnings& warnings);

}  // namespace sh::save
