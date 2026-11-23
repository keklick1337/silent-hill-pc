// The end-of-game ranking (src/engine/screens/ranking.h), worked out by hand from the specification.

#include "engine/screens/ranking.h"

#include "framework/sh_test.h"

#include <cstring>

namespace sc = sh::screens;

SH_TEST(ranking_perfect_run_scores_100) {
    sc::RankingSave s;
    s.current_ending_flags = 1;       // good+ ending: 10
    s.picked_up_items = 150;          // 150 / 15 = 10: 10
    s.save_count = 2;                 // < 3: 5
    s.continue_count = 1;             // < 2: 5
    s.play_time = 60u * 60 << 12;     // 60 minutes < 90: 10
    s.clear_count = 5;                // 10
    s.special_items = 5;              // 10
    s.shots_fired = 0;                // no shots: 10
    s.melee_kills = 200;              // 200 / 5 = 40, shooting 100 / 5 = 20 (halved): 30
    sc::Ranking r;
    r.evaluate(s);
    SH_CHECK(r.score == 100);
    SH_CHECK(r.colors.ending == 0 && r.colors.items == 0 && r.colors.save_count == 0 && r.colors.enemy_kills == 0);
    SH_CHECK(r.colors.shot_percent == 0 && r.hours == 1 && r.minutes == 0);
    const sc::RankingCarryOver c = sc::carry_over(r, 0);
    SH_CHECK(c.set_special_items && c.special_items == 16);
    SH_CHECK(c.current_ending_flags == static_cast<std::int8_t>(64 | 32));
}

SH_TEST(ranking_shot_percentages_and_text) {
    sc::RankingSave s;
    s.shots_fired = 200;
    s.close_hits = 50;   // 25%
    s.mid_hits = 30;
    s.far_hits = 10;     // mid + far = 20%
    s.long_hits = 20;    // 10%
    sc::Ranking r;
    r.evaluate(s);
    SH_CHECK(r.has_fired_shot);
    SH_CHECK(r.close_percent == 25 && r.mid_percent == 20 && r.long_percent == 10 && r.no_aim_percent == 45);
    SH_CHECK(std::strcmp(r.percent_text[0], "0.25") == 0);
    SH_CHECK(std::strcmp(r.percent_text[3], "0.45") == 0);
    // No shot next time: the percentages stay.
    s.shots_fired = 0;
    r.evaluate(s);
    SH_CHECK(!r.has_fired_shot && r.close_percent == 25);
}

SH_TEST(ranking_easy_and_distances) {
    sc::RankingSave s;
    s.difficulty = -1;
    s.save_count = 1200;  // clamped to 999
    s.walk_distance = 12345u << 12;
    sc::Ranking r;
    r.evaluate(s);
    SH_CHECK(r.save_count == 999 && r.walk_km == 12 && r.walk_m == 345);
    SH_CHECK(r.score >= 0);
}

SH_TEST(ranking_kill_counts_pack_nibbles) {
    std::uint8_t melee = 0, ranged = 0, shared = 0;
    sc::store_kill_count(melee, shared, false, 0x234);
    sc::store_kill_count(ranged, shared, true, 0x567);
    SH_CHECK(sc::kill_count(melee, shared, false) == 0x234);
    SH_CHECK(sc::kill_count(ranged, shared, true) == 0x567);
    sc::store_kill_count(melee, shared, false, 9999);
    SH_CHECK(sc::kill_count(melee, shared, false) == sc::kKillsMax);
}

SH_TEST(ranking_count_attack_rules) {
    sc::AttackCounters c;
    sc::count_attack(c, 1, true, 0x10000);  // distance capped at 0x8000
    SH_CHECK(c.shots_fired == 1 && c.mid_hits == 1 && c.mid_kill_hits == 1 && c.hit_distance_total == 0x800);
    sc::count_attack(c, 7, false, 0);
    SH_CHECK(c.other_attacks == 1 && c.melee_hits == 1 && c.melee_kill_hits == 0 && c.shots_fired == 1);
    sc::count_attack(c, 5, false, 100);  // no distance
    SH_CHECK(c.shots_fired == 2 && c.hit_distance_total == 0x800);
    c.shots_fired = 50000;
    sc::count_attack(c, 0, false, 100);  // full: nothing counted
    SH_CHECK(c.close_hits == 0);
}
