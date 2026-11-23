// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/screens/ranking.h"

#include "engine/screens/screens_view.h"

#include <algorithm>

namespace sh::screens {
namespace {

// The USA release counts the Hyper Blaster colour bit among the special items as well (later releases mask it out
// with 0x7); kept, it shows on the sheet.
constexpr std::uint8_t kSpecialItemsMask = 0xF;
constexpr std::uint16_t kCounterMax = 50000;

void write_percent(char* out, std::uint32_t percent) {
    out[0] = static_cast<char>((percent / 100) % 10 + '0');
    out[2] = static_cast<char>((percent / 10 - (percent / 100) * 10) + '0');
    out[3] = static_cast<char>(percent % 10 + '0');
}

// Points for a count under the thresholds (fewer is better): the best band also earns the best colour.
int banded(int value, std::uint8_t& color, const int (&limits)[5], const int (&points)[5]) {
    for (int i = 0; i < 5; ++i) {
        if (value < limits[i]) {
            if (i == 0) color = 0;
            return points[i];
        }
    }
    return 0;
}

}  // namespace

void Ranking::evaluate(const RankingSave& s) {
    int total = 0;
    difficulty = static_cast<std::int8_t>(s.difficulty);
    save_count = std::clamp<std::int16_t>(s.save_count, 0, 999);
    continue_count = s.continue_count;

    const std::uint32_t secs = s.play_time >> 12;
    hours = static_cast<std::uint16_t>(s.extra_hours * 24 + secs / 3600);
    const std::uint32_t walk = s.walk_distance >> 12;
    const std::uint32_t run = s.run_distance >> 12;
    walk_km = static_cast<std::uint16_t>(walk / 1000);
    walk_m = static_cast<std::uint16_t>(walk - walk / 1000 * 1000);
    run_km = static_cast<std::uint16_t>(run / 1000);
    run_m = static_cast<std::uint16_t>(run - run / 1000 * 1000);
    items = static_cast<std::uint16_t>(s.picked_up_items);
    minutes = static_cast<std::uint8_t>((secs / 60) % 60);
    seconds = static_cast<std::uint8_t>(secs - secs / 60 * 60);
    clear_count = s.clear_count;
    clear_endings = s.endings;
    current_ending_flags = static_cast<std::uint8_t>(s.current_ending_flags);
    used_gasoline_on_chainsaw = s.used_gasoline_on_chainsaw ? 1 : 0;
    used_gasoline_on_rock_drill = s.used_gasoline_on_rock_drill ? 1 : 0;
    kills_shooting = static_cast<std::uint16_t>(kill_count(s.ranged_kills, s.melee_kills_b, true));
    kills_melee = static_cast<std::uint16_t>(kill_count(s.melee_kills, s.melee_kills_b, false));

    has_fired_shot = s.shots_fired != 0;
    if (has_fired_shot) {
        const std::uint32_t fired = s.shots_fired;
        close_percent = static_cast<std::uint32_t>(s.close_hits) * 100 / fired;
        mid_percent = (static_cast<std::uint32_t>(s.mid_hits) + s.far_hits) * 100 / fired;
        long_percent = static_cast<std::uint32_t>(s.long_hits) * 100 / fired;
        // Wraps when far hits push the hit share past 100, as the game's unsigned sum did.
        no_aim_percent = 100 - close_percent - mid_percent - long_percent;
        write_percent(percent_text[0], close_percent);
        write_percent(percent_text[1], mid_percent);
        write_percent(percent_text[2], long_percent);
        write_percent(percent_text[3], no_aim_percent);
    }

    const auto total_minutes = static_cast<std::int16_t>(minutes + hours * 60);
    special_items = s.special_items & kSpecialItemsMask;
    end_location = static_cast<std::uint8_t>(s.location);

    carousel_mask = 7;
    if ((s.extra_options & 0x3) && (s.extra_options & 0xC)) carousel_mask = 0xF;
    if (s.extra_options & 1) carousel_mask |= 0x10;
    if (s.hyper_blaster_locked || (current_ending_flags & 0x10)) carousel_mask |= 0x20;

    colors = RankingColors{};
    switch (current_ending_flags) {
        case 1: colors.ending = 0; total += 10; break;
        case 2: total += 5; break;
        case 4: total += 3; break;
        case 8: total += 1; break;
        default: break;
    }

    if (items / 15 >= 10) {
        colors.items = 0;
        total += 10;
    } else {
        total += items / 15;
    }

    total += banded(save_count, colors.save_count, {3, 6, 11, 21, 31}, {5, 4, 3, 2, 1});
    total += banded(continue_count, colors.continue_count, {2, 4, 6, 8, 10}, {5, 4, 3, 2, 1});
    total += banded(total_minutes, colors.play_time, {90, 180, 240, 360, 720}, {10, 5, 3, 2, 1});

    if (clear_count >= 5) {
        colors.clear_count = 0;
        total += 10;
    } else {
        total += clear_count * 2;
    }
    if (special_items >= 5) {
        colors.special_items = 0;
        total += 10;
    } else {
        total += special_items * 2;
    }

    if (has_fired_shot) {
        // Unsigned in the game: a wrapped no-aim share turns into a large positive weight here.
        const auto weighted =
            static_cast<std::int32_t>(close_percent + mid_percent * 2 + long_percent * 3 - no_aim_percent * 4);
        const int points = std::clamp(weighted / 10, 0, 10);
        total += points;
        if (points == 10) colors.shot_percent = 0;
    } else {
        colors.shot_percent = 0;
        total += 10;
    }

    // Kills per attack beyond an allowance that grows with each clear; the smaller of the two rates counts half.
    int per = s.other_attacks - 15 - clear_count * 5;
    if (per < 0) per = 0;
    per += 5;
    const std::uint16_t shooting = has_fired_shot ? kills_shooting : 100;
    int shoot_rate = shooting / per;
    int melee_rate = kills_melee / per;
    if (melee_rate < shoot_rate) {
        shoot_rate /= 2;
    } else {
        melee_rate /= 2;
    }
    const int kill_points = std::clamp(shoot_rate + melee_rate, 0, 30);
    total += kill_points;
    if (kill_points == 30) colors.enemy_kills = 0;

    if (difficulty == -1) total -= 5;
    score = static_cast<std::int8_t>(std::clamp(total, 0, 100));
}

Ranking& ranking() {
    static Ranking r;
    return r;
}

RankingCarryOver carry_over(const Ranking& r, std::uint8_t extra_options) {
    RankingCarryOver c;
    c.location = r.end_location;
    c.clear_count = r.clear_count;
    c.endings = r.clear_endings;
    c.current_ending_flags = static_cast<std::int8_t>(((r.current_ending_flags & 0x10) * 8) + 64);
    c.keep_chainsaw_gasoline = r.used_gasoline_on_chainsaw != 0;
    c.keep_rock_drill_gasoline = r.used_gasoline_on_rock_drill != 0;
    c.unlock_extra_carousel = (extra_options & 0x3) && (extra_options & 0xC);
    c.unlock_first_extra = (extra_options & 1) != 0;
    if (r.score >= 80) {
        c.set_special_items = true;
        c.special_items = 1 << 3;
        if (r.score == 100) {
            c.current_ending_flags = static_cast<std::int8_t>(c.current_ending_flags | (1 << 5));
            c.special_items = 1 << 4;
        }
    }
    return c;
}

std::uint32_t kill_count(std::uint8_t kills, std::uint8_t shared_high, bool ranged) {
    const std::uint32_t high = ranged ? (shared_high & 0xF0u) * 16 : (shared_high & 0xFu) << 8;
    return std::min<std::uint32_t>(kills + high, kKillsMax);
}

void store_kill_count(std::uint8_t& kills, std::uint8_t& shared_high, bool ranged, std::uint32_t count) {
    count = std::min(count, kKillsMax);
    kills = static_cast<std::uint8_t>(count);
    if (ranged) {
        shared_high = static_cast<std::uint8_t>((shared_high & 0x0F) | ((count >> 4) & 0xF0));
    } else {
        shared_high = static_cast<std::uint8_t>((shared_high & 0xF0) | ((count >> 8) & 0x0F));
    }
}

void count_attack(AttackCounters& c, int kind, bool hit, std::int32_t distance) {
    auto bump = [](std::uint16_t& v) {
        if (v < kCounterMax) {
            ++v;
            return true;
        }
        return false;
    };
    switch (kind) {
        case 4:
            bump(c.other_attacks);
            [[fallthrough]];
        case 0:
        case 1:
        case 2:
        case 3:
        case 5:
            if (!bump(c.shots_fired)) break;
            if (kind != 5) {
                distance = std::min<std::int32_t>(distance, 0x8000);
                c.hit_distance_total = (c.hit_distance_total + static_cast<std::uint32_t>(distance >> 4)) & 0x0FFFFFFFu;
            }
            switch (kind) {
                case 0: bump(c.close_hits); break;
                case 1:
                    bump(c.mid_hits);
                    if (hit) bump(c.mid_kill_hits);
                    break;
                case 2:
                    bump(c.long_hits);
                    if (hit) bump(c.long_kill_hits);
                    break;
                case 3:
                    bump(c.far_hits);
                    if (hit) bump(c.far_kill_hits);
                    break;
                default: break;
            }
            break;
        case 7:
            bump(c.other_attacks);
            [[fallthrough]];
        case 6:
            bump(c.melee_hits);
            if (hit) bump(c.melee_kill_hits);
            break;
        default:
            break;
    }
}

}  // namespace sh::screens
