// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Rules of the map NPCs: when a spawn row of the map becomes a live character, when a live
// monster is freed or hidden, which monsters the radio static follows, and the small arithmetic the game's
// bookkeeping (kill credit, saved kills) needs. Numbers are data: gamedata/npc/rules.json.
//
// The module decides; the game side owns the records. It reaches the
// game through NpcHost: positions, map flags, the camera, the sound system and the creation of a character.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::entities {

struct NpcRules {
    struct Spawn {
        std::int32_t ring_outer = 22;          // m
        std::int32_t ring_inner = 20;          // m, room entry only
        std::int32_t long_view_outer = 30;     // m
        std::int32_t long_view_inner = 12;     // m, room entry only
        std::int32_t cooldown_ticks = 60;
        std::int32_t frame_budget_vblanks = 4;  // the pass stops when the frame has used this many
    } spawn;
    struct Despawn {
        std::int32_t alive = 40;
        std::int32_t corpse = 24;
        std::int32_t long_view_alive = 48;
        std::int32_t long_view_corpse = 24;
        std::int32_t long_view_hard = 80;
        std::int32_t hide_distance = 15;
    } despawn;
    std::int32_t radio_range_sq = 1024;          // m^2
    std::int32_t kill_persist_threshold = 1228;  // Q12 turn
    std::int32_t idle_clip_duration = 20480;     // Q12 seconds
    struct IdleClip {
        std::string chara;  // "silenthill:Cybil"
        std::int32_t first = 0, last = 0;
        std::int32_t number = -1;  // the engine's number, filled by resolve_idle_clips
    };
    std::vector<IdleClip> idle_clips;
    // Characters whose state machine is restarted on a first behaviour tick, so its entry status fires once.
    // `session`: the first tick the character runs while the game is up; `spawn`: once per spawn of the character
    // in one of the first `spawn_slots` NPC slots, while its control state is still zero (a cutscene hand-over sets
    // the step itself).
    struct StateRestart {
        std::vector<std::string> session{"silenthill:Cheryl"};
        std::vector<std::string> spawn{"silenthill:Cybil", "silenthill:AirScreamer"};
        std::int32_t spawn_slots = 3;
        std::vector<std::int32_t> session_numbers, spawn_numbers;  // filled by resolve
    } state_restart;
    // A kill credits the ranking's melee or ranged kill count when the killing attack is one of the player's
    // (`player_attacks` first entries of the attack table); an attack whose table state is `ranged_state` is ranged.
    struct KillTally {
        std::int32_t player_attacks = 39;
        std::int32_t ranged_state = 3;
    } kill_tally;
};

Result<NpcRules> parse_npc_rules(std::string_view json_text);

// ---- pure arithmetic -------------------------------------------------------------------------------------------

// True when (ax,az) is farther than `radius` (Q12) from (bx,bz): cheap per-axis rejection, then the exact test.
bool outside_radius(std::int32_t ax, std::int32_t az, std::int32_t bx, std::int32_t bz, std::int32_t radius);

// Square metres between two Q12 points, each axis shifted to Q6 first (the NPC tracking distance).
std::uint32_t tracked_distance_sq(std::int32_t ax, std::int32_t az, std::int32_t bx, std::int32_t bz);
// Square metres, the shift taken after the difference (the camera distance).
std::int32_t shifted_difference_sq(std::int32_t ax, std::int32_t az, std::int32_t bx, std::int32_t bz);

// The record-bit tests. `mask` is the character's heading word read as flags (the record keeps them there).
bool kill_credit_due(std::int32_t health, std::uint32_t mask);
bool damage_pending(std::int32_t damage_amount);
// A kill of an enemy is written to the save unless the difficulty is above normal and the roll is under the
// threshold; `roll` is only consulted (and so only drawn) when the difficulty is above normal.
// Which ranking kill count a kill by attack `attack` credits: -1 none, 0 melee, 1 ranged. `state_of` reads the
// attack table's state of an attack (only consulted for the player's attacks).
template <typename StateOf>
int kill_tally_counter(const NpcRules& r, std::int32_t attack, StateOf&& state_of) {
    if (attack >= r.kill_tally.player_attacks) {
        return -1;
    }
    return state_of(attack) == r.kill_tally.ranged_state ? 1 : 0;
}
template <typename Roll>
bool saved_kill_persists(const NpcRules& r, bool above_normal, Roll&& roll) {
    return !above_normal || roll() >= r.kill_persist_threshold;
}

// ---- the radio tracker ---------------------------------------------------------------------------------------------

constexpr int kRadioEntries = 3;
constexpr int kRadioSlots = 2;
constexpr int kNoSlot = -1;

struct RadioEntry {
    std::int32_t slot = kNoSlot;  // the NPC slot
    std::uint32_t distance_sq = 0;
    std::int32_t x = 0, z = 0;
};

// The three nearest monsters inside the radio's reach, nearest first.
class RadioTracker {
public:
    explicit RadioTracker(std::uint32_t range_sq) : range_sq_(range_sq) { reset(); }
    void reset();
    // Offers a monster that already passed the health, radio and cell tests.
    void offer(int slot, std::uint32_t distance_sq, std::int32_t x, std::int32_t z);
    const RadioEntry& entry(int i) const { return entries_[static_cast<std::size_t>(i)]; }
    // 0 when nobody is tracked, else 1 + the last filled entry.
    int pitch_state() const;
    // The entry index of NPC `slot`, or kNoSlot.
    int index_of(int slot) const;
    // Takes the first free entry whose NPC is not in `taken` (a bit set of NPC slots), marks it; kNoSlot if none.
    int take_free(std::uint32_t& taken) const;

private:
    std::uint32_t range_sq_;
    std::array<RadioEntry, kRadioEntries> entries_{};
};

// What each of the radio's two static channels was following.
struct RadioChannel {
    std::int32_t playing = kNoSlot;  // the NPC it sounded for last tick
    std::int32_t target = kNoSlot;   // the NPC it follows now
    std::int32_t entry = kNoSlot;    // that NPC's tracker entry
    std::int8_t spare = 0;
};

}  // namespace sh::entities
