// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The running progress record (Session::game.save) by readable names. An engine
// class: sh::save::SaveGame converts it to and from its save files (which store names) by field, and the console's
// byte form of it (memory cards, demo setups) is read and written only by engine/save/console_records.cpp.

#pragma once

#include <cstdint>

namespace sh::game {

struct SaveItem {
    std::int32_t item;     // the item's run-time number (0xFF: empty)
    std::int32_t count;
    std::uint8_t command;  // the inventory command of the item
    std::uint8_t extra;
    bool operator==(const SaveItem&) const = default;
};

struct SaveRecord {
    static constexpr int kItemSlots = 40;
    static constexpr int kEnemyStateMaps = 45;
    static constexpr int kEventFlagWords = 52;
    static constexpr int kCounters = 11;

    SaveItem items[kItemSlots];
    std::int8_t player_reset;
    std::int32_t map;   // map index (the map registry's numbering)
    std::int32_t room;
    std::int16_t save_count;
    std::int8_t location;  // save point (save_game.cpp's fixed list of 25)
    std::int32_t paper_map;
    std::int32_t equipped_weapon;  // item number
    std::uint8_t slot_count;
    std::uint32_t item_toggles;  // bit 0: radio on
    // Per map: bits of the enemies still alive (-1 all).
    std::int32_t enemy_states[kEnemyStateMaps];
    std::uint32_t paper_maps;  // bit per paper map held
    std::uint32_t event_flags[kEventFlagWords];
    std::int32_t health_saturation;
    std::int16_t picked_up_items;
    std::uint8_t inventory_flags;
    std::int32_t health;
    std::int32_t position_x;
    std::int16_t rotation_y;
    std::uint8_t clear_count;
    std::uint8_t endings;  // endings reached (bit 4: UFO)
    std::int32_t position_z;
    std::uint32_t play_time;  // Q12 seconds
    std::uint32_t run_distance;
    std::uint32_t walk_distance;
    std::uint8_t mode_bits;
    std::uint8_t melee_kills;
    std::uint8_t melee_kills_b;
    std::uint8_t ranged_kills;
    // Bits 0..27: summed hit distance; bits 28..31: the action difficulty (signed).
    std::uint32_t hits_and_difficulty;
    std::uint16_t counters[kCounters];
    std::int8_t current_ending;
    std::uint8_t continues;

    bool operator==(const SaveRecord&) const = default;

    int difficulty() const { return static_cast<std::int32_t>(hits_and_difficulty) >> 28; }
    void set_difficulty(int value) {
        hits_and_difficulty = (hits_and_difficulty & 0x0FFFFFFFu) | (static_cast<std::uint32_t>(value) << 28);
    }
    bool event_flag(int flag) const { return (event_flags[flag >> 5] >> (flag & 31) & 1u) != 0; }
    void set_event_flag(int flag, bool on) {
        const std::uint32_t bit = 1u << (flag & 31);
        if (on)
            event_flags[flag >> 5] |= bit;
        else
            event_flags[flag >> 5] &= ~bit;
    }
};

namespace save_bits {
constexpr std::uint32_t kRadioOn = 1u << 0;
constexpr std::uint8_t kEndingUfo = 1u << 4;
}  // namespace save_bits

}  // namespace sh::game
