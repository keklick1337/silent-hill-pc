// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ranking against the running game (engine/screens/screens_view.h): evaluated from the
// progress record as the results screen opens, the next game's carry-over written back, and the attack and kill
// counters the record keeps while the game is played.

#include <cstdint>

#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/inventory/game/item_stage.h"
#include "engine/screens/screens_view.h"

// The inventory's unlock test of the Hyper Blaster: -1 when there
// is no such item to unlock.
extern "C" std::int32_t Inventory_HyperBlasterUnlockTest(void);

namespace sh::screens {
namespace {

// Event flags the ranking reads and the carry-over sets (gamedata/flags).
constexpr const char* kFlagChainsawGasoline = "M2S00_UsedGasolineOnChainsaw";
constexpr const char* kFlagRockDrillGasoline = "M2S00_UsedGasolineOnRockDrill";
constexpr const char* kFlagNextGame = "M0S02_UnlockedGasolineTank";
constexpr const char* kFlagExtraCarousel = "M2S00_UnlockedKatanaRoom";
constexpr const char* kFlagFirstExtra = "M0S02_UnlockedChannelingStone";
bool flag_of(const game::SaveRecord& s, const char* name) {
    const int bit = sh::ids::flag_number(name);
    return bit > 0 && s.event_flag(bit);
}
void set_flag_of(game::SaveRecord& s, const char* name) {
    if (const int bit = sh::ids::flag_number(name); bit > 0) {
        s.set_event_flag(bit, true);
    }
}
constexpr std::int8_t kFirstMap = 1;  // the map registry's MAP0_S01

// mode_bits: bit 0 the next game is a "fear" replay, bits 1..2 the 290-hour overflow, bits 3..7 the special items.
constexpr std::uint8_t kNextFearMode = 1u << 0;
int extra_hours(const game::SaveRecord& s) { return (s.mode_bits >> 1) & 3; }
int special_items(const game::SaveRecord& s) { return s.mode_bits >> 3; }
void set_special_items(game::SaveRecord& s, int count) {
    s.mode_bits = static_cast<std::uint8_t>((s.mode_bits & 7u) | ((count & 31) << 3));
}

// The record's counters, in their order after the summed hit distance.
enum Counter { Fired, Close, Mid, Long, Far, MidKill, LongKill, FarKill, Melee, MeleeKill, Other };

constexpr std::uint32_t kHitDistanceMask = 0x0FFFFFFFu;

}  // namespace

void evaluate_ranking() {
    const game::SaveRecord& sv = game::save_record();
    RankingSave s;
    s.difficulty = sv.difficulty();
    s.save_count = sv.save_count;
    s.continue_count = sv.continues;
    s.extra_hours = static_cast<std::uint8_t>(extra_hours(sv));
    s.play_time = sv.play_time;
    s.walk_distance = sv.walk_distance;
    s.run_distance = sv.run_distance;
    s.picked_up_items = sv.picked_up_items;
    s.clear_count = sv.clear_count;
    s.endings = sv.endings;
    s.current_ending_flags = sv.current_ending;
    s.location = sv.location;
    s.special_items = static_cast<std::uint8_t>(special_items(sv));
    s.shots_fired = sv.counters[Fired];
    s.close_hits = sv.counters[Close];
    s.mid_hits = sv.counters[Mid];
    s.long_hits = sv.counters[Long];
    s.far_hits = sv.counters[Far];
    s.other_attacks = sv.counters[Other];
    s.melee_kills = sv.melee_kills;
    s.melee_kills_b = sv.melee_kills_b;
    s.ranged_kills = sv.ranged_kills;
    s.used_gasoline_on_chainsaw = flag_of(sv, kFlagChainsawGasoline);
    s.used_gasoline_on_rock_drill = flag_of(sv, kFlagRockDrillGasoline);
    s.extra_options = game::session().game.settings.extra_options;
    s.hyper_blaster_locked = Inventory_HyperBlasterUnlockTest() == -1;

    Ranking& r = ranking();
    r.evaluate(s);
    sh::inventory::item_stage().results_mask = r.carousel_mask;
}

void apply_ranking_carry_over() {
    const RankingCarryOver c = carry_over(ranking(), game::session().game.settings.extra_options);
    game::SaveRecord& sv = game::save_record();
    sv.location = static_cast<std::int8_t>(c.location);
    sv.clear_count = c.clear_count;
    sv.endings = c.endings;
    sv.current_ending = c.current_ending_flags;
    sv.mode_bits |= kNextFearMode;
    sv.map = kFirstMap;
    if (c.keep_chainsaw_gasoline) set_flag_of(sv, kFlagChainsawGasoline);
    if (c.keep_rock_drill_gasoline) set_flag_of(sv, kFlagRockDrillGasoline);
    set_flag_of(sv, kFlagNextGame);
    if (c.unlock_extra_carousel) set_flag_of(sv, kFlagExtraCarousel);
    if (c.unlock_first_extra) set_flag_of(sv, kFlagFirstExtra);
    if (c.set_special_items) set_special_items(sv, c.special_items);
}

void count_player_attack(int kind, bool hit, std::int32_t distance) {
    game::SaveRecord& sv = game::save_record();
    AttackCounters c;
    c.hit_distance_total = sv.hits_and_difficulty & kHitDistanceMask;
    c.shots_fired = sv.counters[Fired];
    c.close_hits = sv.counters[Close];
    c.mid_hits = sv.counters[Mid];
    c.long_hits = sv.counters[Long];
    c.far_hits = sv.counters[Far];
    c.mid_kill_hits = sv.counters[MidKill];
    c.long_kill_hits = sv.counters[LongKill];
    c.far_kill_hits = sv.counters[FarKill];
    c.melee_hits = sv.counters[Melee];
    c.melee_kill_hits = sv.counters[MeleeKill];
    c.other_attacks = sv.counters[Other];
    count_attack(c, kind, hit, distance);
    sv.hits_and_difficulty = (sv.hits_and_difficulty & ~kHitDistanceMask) | (c.hit_distance_total & kHitDistanceMask);
    sv.counters[Fired] = c.shots_fired;
    sv.counters[Close] = c.close_hits;
    sv.counters[Mid] = c.mid_hits;
    sv.counters[Long] = c.long_hits;
    sv.counters[Far] = c.far_hits;
    sv.counters[MidKill] = c.mid_kill_hits;
    sv.counters[LongKill] = c.long_kill_hits;
    sv.counters[FarKill] = c.far_kill_hits;
    sv.counters[Melee] = c.melee_hits;
    sv.counters[MeleeKill] = c.melee_kill_hits;
    sv.counters[Other] = c.other_attacks;
}

void add_enemy_kill(bool ranged) {
    game::SaveRecord& sv = game::save_record();
    std::uint8_t& kills = ranged ? sv.ranged_kills : sv.melee_kills;
    std::uint8_t shared = sv.melee_kills_b;
    store_kill_count(kills, shared, ranged, kill_count(kills, shared, ranged) + 1);
    sv.melee_kills_b = shared;
}

}  // namespace sh::screens
