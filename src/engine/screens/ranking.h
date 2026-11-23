// SPDX-License-Identifier: GPL-3.0-or-later
//
// The end-of-game ranking: the statistics the results sheet shows, the score out
// of 100 and its star colours, the save carried into the next game, and the attack and kill counters the game keeps
// in the save while it is played. Pure: the host reads and writes the save.
#pragma once

#include <cstdint>

namespace sh::screens {

// What the ranking reads from the save (sh::save::SaveGame names) and the game options.
struct RankingSave {
    int difficulty = 0;  // -1 easy, 0 normal, 1 hard
    std::int16_t save_count = 0;
    std::uint8_t continue_count = 0;
    std::uint8_t extra_hours = 0;    // 0..3 (the save's 290-hour overflow bits)
    std::uint32_t play_time = 0;     // Q12 seconds
    std::uint32_t walk_distance = 0, run_distance = 0;  // Q12 metres
    std::int16_t picked_up_items = 0;
    std::uint8_t clear_count = 0;
    std::uint8_t endings = 0;
    std::int8_t current_ending_flags = 0;
    std::int8_t location = 0;
    std::uint8_t special_items = 0;  // 5 bits
    std::uint16_t shots_fired = 0, close_hits = 0, mid_hits = 0, long_hits = 0, far_hits = 0, other_attacks = 0;
    std::uint8_t melee_kills = 0, melee_kills_b = 0, ranged_kills = 0;
    bool used_gasoline_on_chainsaw = false;   // event flags the next game keeps
    bool used_gasoline_on_rock_drill = false;
    std::uint8_t extra_options = 0;           // the options' unlocked extras
    bool hyper_blaster_locked = false;        // the inventory test found no Hyper Blaster to unlock
};

// Star colour of a results row: 0 is the best mark, 7 the plain one.
struct RankingColors {
    std::uint8_t ending = 7, clear_count = 7, save_count = 7, continue_count = 7, play_time = 7, items = 7,
                 special_items = 7, enemy_kills = 7, shot_percent = 7;
};

class Ranking {
public:
    // Fills the sheet from the save. The shot percentages (and their digit strings) keep their last values when
    // no shot was fired, as the game's did.
    void evaluate(const RankingSave& save);

    std::int8_t difficulty = 0;
    std::int16_t save_count = 0;
    std::uint8_t continue_count = 0;
    std::uint16_t hours = 0;
    std::uint8_t minutes = 0, seconds = 0;
    std::uint16_t walk_km = 0, walk_m = 0, run_km = 0, run_m = 0;
    std::uint16_t items = 0;
    std::uint8_t clear_count = 0, clear_endings = 0, current_ending_flags = 0;
    std::uint8_t end_location = 0;
    std::uint8_t special_items = 0;
    std::int8_t score = 0;
    std::uint8_t used_gasoline_on_chainsaw = 0, used_gasoline_on_rock_drill = 0;
    std::uint16_t kills_shooting = 0, kills_melee = 0;
    bool has_fired_shot = false;
    std::uint32_t close_percent = 0, mid_percent = 0, long_percent = 0, no_aim_percent = 0;
    char percent_text[4][5] = {"0.00", "0.00", "0.00", "0.00"};  // close, mid, long, no aim: "D.DD"
    RankingColors colors;
    std::uint8_t carousel_mask = 7;  // the extra items the next game's carousel shows
};

// The save the next game starts from (written over the finished one).
struct RankingCarryOver {
    std::uint8_t location = 0;
    std::uint8_t clear_count = 0;
    std::uint8_t endings = 0;
    std::int8_t current_ending_flags = 0;
    bool keep_chainsaw_gasoline = false;     // set the flag again
    bool keep_rock_drill_gasoline = false;
    bool unlock_extra_carousel = false;      // both groups of extras: the flag of the full carousel
    bool unlock_first_extra = false;
    bool set_special_items = false;          // score >= 80: the Hyper Blaster colour
    std::uint8_t special_items = 0;
};
RankingCarryOver carry_over(const Ranking& ranking, std::uint8_t extra_options);

// Enemy kills, packed in the save as two bytes and a shared byte of high nibbles; at most 4000 each.
constexpr std::uint32_t kKillsMax = 4000;
std::uint32_t kill_count(std::uint8_t kills, std::uint8_t shared_high, bool ranged);
// Stores `count` (clamped) into the kills byte and its nibble of the shared byte.
void store_kill_count(std::uint8_t& kills, std::uint8_t& shared_high, bool ranged, std::uint32_t count);

// The attack counters of the save (sh::save::SaveGame names).
struct AttackCounters {
    std::uint32_t hit_distance_total = 0;  // 28 bits
    std::uint16_t shots_fired = 0, close_hits = 0, mid_hits = 0, long_hits = 0, far_hits = 0;
    std::uint16_t mid_kill_hits = 0, long_kill_hits = 0, far_kill_hits = 0;
    std::uint16_t melee_hits = 0, melee_kill_hits = 0, other_attacks = 0;
    bool operator==(const AttackCounters&) const = default;
};
// One attack of the player: `kind` 0..3 a shot hitting at close, mid, long, far range, 4 a shot of the other kind,
// 5 a shot without a distance, 6 a melee attack, 7 a melee attack of the other kind; `hit` when it hit a target;
// `distance` (Q12) of the shot. Every counter stops at 50000.
void count_attack(AttackCounters& counters, int kind, bool hit, std::int32_t distance);

}  // namespace sh::screens
