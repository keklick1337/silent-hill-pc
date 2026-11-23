// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The attack table of the running game: one row per packed attack, read from
// gamedata/config/attacks.json at boot, the weapons' own rows (gamedata/weapons/<Name>.json "attacks") over it.
// The rows keep the game's 24-byte layout: the combat readers index them by number.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

#include "engine/items/item_library.h"

namespace sh::inventory::game {

struct AttackRow {
    std::int16_t reach;        // Q12: far end of the hit segment / ray length
    std::int16_t reach_start;  // Q12: near end of the hit segment
    std::uint16_t power;
    std::uint8_t shape[6];     // read two ways by hit_type (sh::items::WeaponAttack)
    std::int16_t knockback;    // Q12
    std::uint8_t keyframe_start;
    std::uint8_t keyframe_length;
    std::uint8_t hit_type;
    std::uint8_t hurt_sound;   // 0xFF: none
    std::uint8_t blood_splatter;
    std::uint8_t reserved;
    const std::uint32_t* unused_link;  // every row points at one word nothing reads
};

inline constexpr int kAttackRows = 70;  // the original's rows (gamedata/config/attacks.json)
// Rows the table holds from the start; past kAttackRows only the rows of a mod's weapon (set_attack_row), which grow
// the table to any packed attack a mod's weapon takes.
inline constexpr int kInitialAttackRows = 128;

// The table (static storage, filled before any code runs).
std::vector<AttackRow>& attack_table();
// Rows in use: kAttackRows, or one past the last row a mod's gun set.
int attack_row_count();

// Reads gamedata/config/attacks.json (the last mod that ships it wins); the table stays as it is on an error.
void load_attack_table(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir);

// The index of the row named `name` in attacks.json ("StalkerLunge", "silenthill:StalkerLunge"), -1 when no row has
// it. Scripts resolve their attack names once through this; the number is never stored.
int attack_number(std::string_view name);

// A weapon's attack over its row (a mod's weapon appends rows past kAttackRows); false for a negative index.
bool set_attack_row(const items::WeaponAttack& attack);
// The weapon's own row set there (with its combat rules), nullptr for a row no weapon gave.
const items::WeaponAttack* weapon_attack(int index);

}  // namespace sh::inventory::game
