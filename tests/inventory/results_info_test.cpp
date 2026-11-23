// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests for the results sheet's data reader and layout records
// (sh::inventory::parse_results_info / layout_results_info); expected values
// are the original results screen's layout.

#include "engine/inventory/results_info.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include "framework/sh_test.h"

using namespace sh::inventory;

namespace {

// The shipped sheet's values, compact; parse must accept exactly these keys.
constexpr const char* kJson = R"JSON({
  "colors": { "white": 7, "rank_perfect": 0 },
  "items_total": 204,
  "labels": {
    "game_result": { "text": "GAME_RESULT", "x": 100, "y": -84, "budget": 20 },
    "mode": { "text": "Mode", "x": 24, "y": -56, "budget": 20 },
    "difficulty_easy": { "text": "EASY", "x": 252, "y": -56, "budget": 20, "align_right": true },
    "difficulty_normal": { "text": "NORMAL", "x": 222, "y": -56, "budget": 20, "align_right": true },
    "difficulty_hard": { "text": "HARD", "x": 247, "y": -56, "budget": 20, "align_right": true },
    "saves": { "text": "Saves", "x": 24, "y": 7, "budget": 20 },
    "continues": { "text": "Continues", "x": 24, "y": 28, "budget": 20 },
    "total_time": { "text": "Total_time", "x": 24, "y": 49, "budget": 20 },
    "hours_unit": { "text": "h", "x": 220, "y": 49, "budget": 5 },
    "minutes_unit": { "text": "m", "x": 254, "y": 49, "budget": 5 },
    "seconds_unit": { "text": "s", "x": 288, "y": 49, "budget": 5 },
    "decimal_point": { "text": ".", "budget": 5 },
    "km_unit": { "text": "km", "budget": 5 },
    "walking_distance": { "text": "Walking_distance", "x": 24, "y": 70, "budget": 20 },
    "running_distance": { "text": "Running_distance", "x": 24, "y": 91, "budget": 20 },
    "items": { "text": "Items", "x": 24, "y": 112, "budget": 20 },
    "slash": { "text": "/", "budget": 5 },
    "plus": { "text": "+", "budget": 5 },
    "game_clear": { "text": "Game_clear", "x": 24, "y": -35, "budget": 20 },
    "ending": { "text": "Ending", "x": 24, "y": -14, "budget": 20 },
    "ending_good_plus": { "text": "GOOD+", "x": 234, "y": -14, "budget": 10 },
    "ending_good": { "text": "GOOD", "x": 244, "y": -14, "budget": 10 },
    "ending_bad_plus": { "text": "BAD+", "x": 249, "y": -14, "budget": 10 },
    "ending_bad": { "text": "BAD", "x": 258, "y": -14, "budget": 10 },
    "ending_ufo": { "text": "UFO", "x": 256, "y": -14, "budget": 10 },
    "your_rank": { "text": "==Your_rank==", "x": 24, "y": 292, "budget": 20 },
    "star": { "text": "@", "budget": 1 },
    "defeated_by_shooting": { "text": "Defeated_\u0001\u0001enemy_\u0001\u0001by_\u0001\u0001shooting", "x": 24, "y": 154, "budget": 32 },
    "defeated_by_fighting": { "text": "Defeated_\u0001\u0001enemy_\u0001\u0001by_\u0001\u0001fighting", "x": 24, "y": 133, "budget": 32 },
    "shooting_style": { "text": "Shooting_style", "x": 24, "y": 175, "budget": 32 },
    "short_range_shots": { "text": "Short_range_shots", "x": 72, "y": 196, "budget": 32 },
    "middle_range_shots": { "text": "Middle_range_shots", "x": 72, "y": 217, "budget": 32 },
    "long_range_shots": { "text": "Long_range_shots", "x": 72, "y": 238, "budget": 32 },
    "no_aiming_shots": { "text": "No_aiming_shots", "x": 72, "y": 259, "budget": 32 },
    "paren_left": { "text": "(", "budget": 1 },
    "paren_right": { "text": ")", "x": 292, "y": 112, "budget": 1 }
  },
  "values": {
    "saves": { "anchor_x": 286, "digit_span": 10, "span": "digits_minus_one", "width_min": 3 },
    "continues": { "anchor_x": 286, "digit_span": 10, "span": "digits_minus_one", "width_min": 3 },
    "hours": { "anchor_x": 220, "digit_span": 10, "span": "digits", "width_min": 5 },
    "minutes": { "anchor_x": 254, "digit_span": 10, "span": "digits", "width_min": 2 },
    "seconds": { "anchor_x": 288, "digit_span": 10, "span": "digits", "width_min": 2 },
    "walk_km": { "anchor_x": 229, "digit_span": 10, "span": "digits_minus_one", "width_min": 4 },
    "run_km": { "anchor_x": 229, "digit_span": 10, "span": "digits_minus_one", "width_min": 4 },
    "items": { "anchor_x": 218, "digit_span": 10, "span": "digits_minus_one", "width_min": 3 },
    "game_clear": { "anchor_x": 286, "digit_span": 10, "span": "digits_minus_one", "width_min": 3 },
    "kills_shooting": { "anchor_x": 296, "digit_span": 10, "span": "digits", "width_min": 4 },
    "kills_fighting": { "anchor_x": 296, "digit_span": 10, "span": "digits", "width_min": 4 }
  },
  "percent_rows": {
    "value_x": 262, "first_value_y": 196, "row_pitch": 21, "value_budget": 5,
    "rows": ["short_range_shots", "middle_range_shots", "long_range_shots", "no_aiming_shots"]
  },
  "rank": {
    "tens_only": { "x": 160, "y": 198, "budget": 1, "hi_res": true },
    "ones_only": { "x": 160, "y": 292, "budget": 1, "hi_res": false },
    "both_tens": { "x": 160, "y": 194, "budget": 20, "hi_res": true },
    "both_ones": { "x": 160, "y": 300, "budget": 20, "hi_res": false }
  },
  "rank_bars": {
    "left_x": -64, "right_x": 66, "top_y": -196, "band_height": 6,
    "easy": [ { "top": [0, 0, 0], "bottom": [128, 192, 0] }, { "top": [128, 192, 0], "bottom": [0, 0, 0] } ],
    "normal": [ { "top": [0, 0, 0], "bottom": [0, 128, 192] }, { "top": [0, 128, 192], "bottom": [0, 0, 0] } ],
    "hard": [ { "top": [0, 0, 0], "bottom": [128, 0, 192] }, { "top": [128, 0, 192], "bottom": [0, 0, 0] } ]
  }
})JSON";

const ResultsInfoData& sheet() {
    static const ResultsInfoData data = [] {
        const sh::Result<ResultsInfoData> parsed = parse_results_info(kJson);
        if (!parsed) {
            return ResultsInfoData{};
        }
        return parsed.value();
    }();
    return data;
}

const ResultsDraw* find_text(const ResultsInfoLayout& layout, ResultsText key) {
    for (const ResultsDraw& d : layout.draws) {
        if (d.kind == ResultsDrawKind::Text && d.key == key) {
            return &d;
        }
    }
    return nullptr;
}

// The first number draw with this value.
const ResultsDraw* find_number(const ResultsInfoLayout& layout, std::int32_t value) {
    for (const ResultsDraw& d : layout.draws) {
        if (d.kind == ResultsDrawKind::Number && d.number == value) {
            return &d;
        }
    }
    return nullptr;
}

// The draw's index in the sequence, for order checks.
std::size_t index_of(const ResultsInfoLayout& layout, ResultsText key) {
    for (std::size_t i = 0; i < layout.draws.size(); i++) {
        if (layout.draws[i].kind == ResultsDrawKind::Text && layout.draws[i].key == key) {
            return i;
        }
    }
    return layout.draws.size();
}

ResultsInfoInput full_input() {
    ResultsInfoInput in;
    in.difficulty = 0;
    in.saves = 42;
    in.continues = 7;
    in.hours = 13;
    in.minutes = 5;
    in.seconds = 9;
    in.walk_km = 1;
    in.walk_fraction = 5;
    in.run_km = 12;
    in.run_fraction = 345;
    in.items = 100;
    in.special_items = 2;
    in.clears = 3;
    in.ending_bits = 4;  // BAD+
    in.rank = 35;
    in.kills_by_shooting = 123;
    in.kills_by_fighting = 71;
    in.fired_shots = true;
    static const char* kPercents[4] = {"3.50", "2.75", "1.00", "9.99"};
    for (int i = 0; i < 4; i++) {
        in.shot_percent[i] = kPercents[i];
    }
    in.color_ending = 0;
    in.color_game_clear = 7;
    in.color_saves = 0;
    in.color_continues = 7;
    in.color_total_time = 7;
    in.color_items = 0;
    in.color_special_items = 7;
    in.color_kills = 7;
    in.color_shooting = 0;
    return in;
}

}  // namespace

SH_TEST(results_info_parse_full_sheet) {
    const ResultsInfoData& data = sheet();
    SH_CHECK(data.labels[static_cast<std::size_t>(ResultsText::GameResult)].text == "GAME_RESULT");
    SH_CHECK(data.labels[static_cast<std::size_t>(ResultsText::GameResult)].x == 100 &&
             data.labels[static_cast<std::size_t>(ResultsText::GameResult)].y == -84);
    // Cursor strings carry no position of their own.
    SH_CHECK(!data.labels[static_cast<std::size_t>(ResultsText::DecimalPoint)].positioned);
    SH_CHECK(data.labels[static_cast<std::size_t>(ResultsText::ParenRight)].positioned &&
             data.labels[static_cast<std::size_t>(ResultsText::ParenRight)].x == 292);
    // Only the difficulty words shift with a longer translation.
    SH_CHECK(data.labels[static_cast<std::size_t>(ResultsText::DifficultyNormal)].align_right);
    SH_CHECK(!data.labels[static_cast<std::size_t>(ResultsText::Mode)].align_right);
    // The markup bytes survive the JSON round-trip.
    const std::string& shooting = data.labels[static_cast<std::size_t>(ResultsText::DefeatedByShooting)].text;
    SH_CHECK(shooting.size() == 32 && shooting[9] == '\x01' && shooting[31] == 'g');
    SH_CHECK(data.items_total == 204 && data.white_color == 7 && data.rank_perfect_color == 0);
    SH_CHECK(data.percent_rows.labels[0] == ResultsText::ShortRangeShots);
    SH_CHECK(data.percent_rows.labels[3] == ResultsText::NoAimingShots);
    SH_CHECK(data.rank_tens_only.hi_res && !data.rank_ones_only.hi_res);
    SH_CHECK(data.rank_both_tens.y == 194 && data.rank_both_ones.y == 300);
    SH_CHECK(data.rank_bars.bands[1][0][1] == (Rgb{0, 128, 192}));
    SH_CHECK(data.rank_bars.bands[1][1][0] == (Rgb{0, 128, 192}));
    SH_CHECK(data.rank_bars.bands[0][0][1] == (Rgb{128, 192, 0}));
}

// The shipped sheet parses whole (it lacked "items_total", so the results screen drew nothing).
SH_TEST(results_info_shipped_file) {
    std::ifstream in(std::string(SH_PROJECT_ROOT) + "/gamedata/menu/results_info.json");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsed = sh::inventory::parse_results_info(text);
    SH_CHECK(static_cast<bool>(parsed));
    SH_CHECK(parsed && parsed.value().items_total == 204);
}

SH_TEST(results_info_parse_rejects_bad_data) {
    sh::Result<ResultsInfoData> bad = parse_results_info(R"({ "colors": {} })");
    SH_CHECK(!bad);
    bad = parse_results_info(R"({ "unexpected": 1 })");
    SH_CHECK(!bad);
    // Every label and value column must be present.
    std::string missing(kJson);
    missing.erase(missing.find("\"ending_ufo\""), 60);
    bad = parse_results_info(missing);
    SH_CHECK(!bad);
}

SH_TEST(results_info_layout_rows_and_right_alignment) {
    const ResultsInfoLayout out = layout_results_info(sheet(), full_input());

    // The header and the mode row.
    const ResultsDraw* title = find_text(out, ResultsText::GameResult);
    SH_CHECK(title != nullptr && title->x == 100 && title->y == -84 && title->budget == 20 && title->color == 7);
    SH_CHECK(find_text(out, ResultsText::DifficultyNormal) != nullptr);
    SH_CHECK(find_text(out, ResultsText::DifficultyEasy) == nullptr);

    // Number columns: right-aligned against their anchors.
    // saves 42: 286 - (2-1)*10; continues 7: 286 - 0; hours 3: 220 - 10; minutes 5: 254 - 10.
    const ResultsDraw* saves = find_number(out, 42);
    SH_CHECK(saves != nullptr && saves->x == 276 && saves->y == 7 && saves->width_min == 3 && saves->color == 0);
    const ResultsDraw* continues = find_number(out, 7);
    SH_CHECK(continues != nullptr && continues->x == 286 && continues->y == 28 && continues->width_min == 3);
    const ResultsDraw* hours = find_number(out, 13);
    SH_CHECK(hours != nullptr && hours->x == 200 && hours->width_min == 5 && hours->y == 49);
    const ResultsDraw* minutes = find_number(out, 5);
    SH_CHECK(minutes != nullptr && minutes->x == 244 && minutes->width_min == 2);
    const ResultsDraw* seconds = find_number(out, 9);
    SH_CHECK(seconds != nullptr && seconds->x == 278);
    // The whole time row draws in the time row's color, units included.
    SH_CHECK(hours->color == 7);
    const ResultsDraw* h_unit = find_text(out, ResultsText::HoursUnit);
    SH_CHECK(h_unit != nullptr && h_unit->x == 220 && h_unit->y == 49 && h_unit->color == 7);

    // Distances: km part right-aligned, fraction zero-padded at the cursor.
    const ResultsDraw* walk_km = find_number(out, 1);
    SH_CHECK(walk_km != nullptr && walk_km->x == 229 && walk_km->y == 70 && walk_km->width_min == 4 &&
             walk_km->color == 7);
    // walk fraction 5: two leading zeros, then the 5; run fraction 345: none.
    const std::size_t dot1 = index_of(out, ResultsText::DecimalPoint);
    SH_CHECK(dot1 < out.draws.size());
    SH_CHECK(out.draws[dot1 + 1].number == 0 && !out.draws[dot1 + 1].positioned && out.draws[dot1 + 1].width_min == 1);
    SH_CHECK(out.draws[dot1 + 2].number == 0 && out.draws[dot1 + 2].width_min == 1);
    SH_CHECK(out.draws[dot1 + 3].number == 5 && out.draws[dot1 + 3].width_min == 3);
    SH_CHECK(out.draws[dot1 + 4].key == ResultsText::KmUnit);
    const ResultsDraw* run_km = find_number(out, 12);
    SH_CHECK(run_km != nullptr && run_km->x == 219 && run_km->y == 91);
    const std::size_t dot2 = dot1 + 7;
    SH_CHECK(out.draws[dot2].key == ResultsText::DecimalPoint);
    SH_CHECK(out.draws[dot2 + 1].number == 345 && out.draws[dot2 + 1].width_min == 3);
    SH_CHECK(out.draws[dot2 + 2].key == ResultsText::KmUnit);

    // Items: count, "/204", the special-items bracket.
    const ResultsDraw* items = find_number(out, 100);
    SH_CHECK(items != nullptr && items->x == 198 && items->y == 112 && items->width_min == 3 && items->color == 0);
    const ResultsDraw* total = find_number(out, 204);
    SH_CHECK(total != nullptr && total->width_min == 3 && total->color == 0 && !total->positioned);
    const std::size_t plus = index_of(out, ResultsText::Plus);
    SH_CHECK(plus < out.draws.size());
    SH_CHECK(out.draws[plus + 1].number == 2 && out.draws[plus + 1].width_min == 1);
    const ResultsDraw* paren_r = find_text(out, ResultsText::ParenRight);
    SH_CHECK(paren_r != nullptr && paren_r->x == 292 && paren_r->y == 112 && paren_r->budget == 1);

    // Ending word by bit, game clears above it.
    const ResultsDraw* word = find_text(out, ResultsText::EndingBadPlus);
    SH_CHECK(word != nullptr && word->x == 249 && word->y == -14 && word->budget == 10 && word->color == 0);
    SH_CHECK(find_text(out, ResultsText::EndingUfo) == nullptr);
    const ResultsDraw* clears = find_number(out, 3);
    SH_CHECK(clears != nullptr && clears->y == -35 && clears->x == 286 && clears->width_min == 3);

    // Kills rows: shooting below fighting, both against anchor 296.
    const ResultsDraw* kills_s = find_number(out, 123);
    SH_CHECK(kills_s != nullptr && kills_s->x == 266 && kills_s->y == 154 && kills_s->width_min == 4);
    const ResultsDraw* kills_f = find_number(out, 71);
    SH_CHECK(kills_f != nullptr && kills_f->x == 276 && kills_f->y == 133 && kills_f->width_min == 4);
    SH_CHECK(index_of(out, ResultsText::DefeatedByShooting) < index_of(out, ResultsText::DefeatedByFighting));

    // The shot-percentage rows: header plus four label/percent pairs.
    const ResultsDraw* style = find_text(out, ResultsText::ShootingStyle);
    SH_CHECK(style != nullptr && style->x == 24 && style->y == 175 && style->budget == 32);
    const std::size_t first_label = index_of(out, ResultsText::ShortRangeShots);
    SH_CHECK(first_label < out.draws.size());
    SH_CHECK(out.draws[first_label + 1].raw_text != nullptr &&
             std::strcmp(out.draws[first_label + 1].raw_text, "3.50") == 0);
    SH_CHECK(out.draws[first_label + 1].x == 262 && out.draws[first_label + 1].y == 196 &&
             out.draws[first_label + 1].budget == 5 && out.draws[first_label + 1].color == 0);
    const std::size_t last_label = index_of(out, ResultsText::NoAimingShots);
    SH_CHECK(out.draws[last_label + 1].y == 259);

    // Draw count for this input: 31 labels + 17 numbers + 4 percents + 8 stars.
    SH_CHECK(out.draws.size() == 60);
}

SH_TEST(results_info_layout_stars) {
    ResultsInfoInput in = full_input();
    in.fired_shots = false;

    // 35 = three tens stars (hi-res) plus five ones stars.
    in.rank = 35;
    ResultsInfoLayout out = layout_results_info(sheet(), in);
    std::size_t stars = 0;
    for (const ResultsDraw& d : out.draws) {
        if (d.kind == ResultsDrawKind::Text && d.key == ResultsText::Star) {
            stars++;
        }
    }
    SH_CHECK(stars == 8);
    SH_CHECK(out.draws.back().y == 300 && out.draws.back().budget == 20 && !out.draws.back().hi_res);

    // 40 = the tens row alone, budget 1.
    in.rank = 40;
    out = layout_results_info(sheet(), in);
    stars = 0;
    for (const ResultsDraw& d : out.draws) {
        if (d.kind == ResultsDrawKind::Text && d.key == ResultsText::Star) {
            stars++;
            SH_CHECK(d.y == 198 && d.budget == 1 && d.hi_res);
        }
    }
    SH_CHECK(stars == 4);

    // 7 = the ones row alone.
    in.rank = 7;
    out = layout_results_info(sheet(), in);
    stars = 0;
    for (const ResultsDraw& d : out.draws) {
        if (d.kind == ResultsDrawKind::Text && d.key == ResultsText::Star) {
            stars++;
            SH_CHECK(d.y == 292 && d.budget == 1 && !d.hi_res);
        }
    }
    SH_CHECK(stars == 7);

    // 100 = perfect color, ten stars in the tens row.
    in.rank = 100;
    out = layout_results_info(sheet(), in);
    stars = 0;
    for (const ResultsDraw& d : out.draws) {
        if (d.kind == ResultsDrawKind::Text && d.key == ResultsText::Star) {
            stars++;
            SH_CHECK(d.color == 0);
        }
    }
    SH_CHECK(stars == 10);

    // 0 = no stars at all.
    in.rank = 0;
    out = layout_results_info(sheet(), in);
    for (const ResultsDraw& d : out.draws) {
        SH_CHECK(d.key != ResultsText::Star);
    }
    // And no shot rows without fired shots.
    SH_CHECK(find_text(out, ResultsText::ShootingStyle) == nullptr);
}

SH_TEST(results_info_layout_rank_bars) {
    // Normal: black top edge, blue bottom edge in band 0, inverted in band 1.
    ResultsInfoInput in = full_input();
    in.difficulty = 0;
    ResultsInfoLayout out = layout_results_info(sheet(), in);
    SH_CHECK(out.quads.size() == 2);
    SH_CHECK(out.quads[0].corners[0] == (Point{-64, -196}) && out.quads[0].corners[3] == (Point{66, -190}));
    SH_CHECK(out.quads[0].colors[0] == (Rgb{0, 0, 0}) && out.quads[0].colors[1] == (Rgb{0, 128, 192}));
    SH_CHECK(out.quads[0].colors[2] == (Rgb{0, 0, 0}) && out.quads[0].colors[3] == (Rgb{0, 128, 192}));
    SH_CHECK(out.quads[1].corners[0] == (Point{-64, -190}) && out.quads[1].corners[3] == (Point{66, -184}));
    SH_CHECK(out.quads[1].colors[0] == (Rgb{0, 128, 192}) && out.quads[1].colors[1] == (Rgb{0, 0, 0}));

    in.difficulty = -1;  // easy: yellow-green
    out = layout_results_info(sheet(), in);
    SH_CHECK(out.quads[0].colors[1] == (Rgb{128, 192, 0}));

    in.difficulty = 1;  // hard: magenta
    out = layout_results_info(sheet(), in);
    SH_CHECK(out.quads[0].colors[1] == (Rgb{128, 0, 192}));

    // A difficulty outside -1..1 draws no bars and no word (garbage colors in
    // the original, unreachable with valid saves).
    in.difficulty = 5;
    out = layout_results_info(sheet(), in);
    SH_CHECK(out.quads.empty());
    SH_CHECK(find_text(out, ResultsText::DifficultyHard) == nullptr);
}

SH_TEST(results_info_layout_items_row_without_special) {
    ResultsInfoInput in = full_input();
    in.special_items = 0;
    const ResultsInfoLayout out = layout_results_info(sheet(), in);
    SH_CHECK(find_text(out, ResultsText::Plus) == nullptr);
    // The bracket closes right after the "/204".
    const std::size_t total = [out] {
        for (std::size_t i = 0; i < out.draws.size(); i++) {
            if (out.draws[i].kind == ResultsDrawKind::Number && out.draws[i].number == 204) {
                return i;
            }
        }
        return out.draws.size();
    }();
    SH_CHECK(total < out.draws.size());
    SH_CHECK(out.draws[total + 1].key == ResultsText::ParenLeft);
    SH_CHECK(out.draws[total + 2].key == ResultsText::ParenRight);
}
