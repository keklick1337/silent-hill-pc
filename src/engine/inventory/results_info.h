// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The results screen's statistics sheet as drawing records, in the draw order
// of the original screen. All texts, placements,
// budgets and the rank-bar colors are game data and come from
// gamedata/menu/results_info.json; the row composition and the right-aligned
// number columns are the layout's own arithmetic.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "engine/inventory/inventory_draw.h"
#include "formats/common/status.h"

namespace sh::inventory {

// Named UI strings of the sheet, resolved from the JSON's "labels" table.
enum class ResultsText : std::uint8_t {
    GameResult, Mode, DifficultyEasy, DifficultyNormal, DifficultyHard,
    Saves, Continues, TotalTime, HoursUnit, MinutesUnit, SecondsUnit,
    DecimalPoint, KmUnit, WalkingDistance, RunningDistance, Items,
    Slash, Plus, GameClear, Ending,
    EndingGoodPlus, EndingGood, EndingBadPlus, EndingBad, EndingUfo,
    YourRank, Star, DefeatedByShooting, DefeatedByFighting, ShootingStyle,
    ShortRangeShots, MiddleRangeShots, LongRangeShots, NoAimingShots,
    ParenLeft, ParenRight,
    Count
};

// The number columns, resolved from the JSON's "values" table.
enum class ResultsValue : std::uint8_t {
    Saves, Continues, Hours, Minutes, Seconds,
    WalkKm, RunKm, Items, GameClear, KillsShooting, KillsFighting,
    Count
};

struct ResultsLabelText {
    std::string text;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t budget = 0;
    bool positioned = true;   // false: drawn at the running cursor
    bool align_right = false; // shift x by the translated width difference (i18n, bridge-side)
};

struct ResultsValueColumn {
    std::int32_t anchor_x = 0;
    std::int32_t digit_span = 0;
    bool span_by_count = false;  // x = anchor - digits*span, else anchor - (digits-1)*span
    std::int32_t width_min = 0;
};

// The four shot-percentage rows: which label each row sits next to.
struct ResultsPercentRows {
    std::array<ResultsText, 4> labels{};
    std::int32_t value_x = 0;
    std::int32_t first_value_y = 0;
    std::int32_t row_pitch = 0;
    std::int32_t value_budget = 0;
};

// A star-row placement (the rank is drawn as rows of the star glyph).
struct ResultsRankRow {
    std::int32_t x = 0, y = 0, budget = 0;
    bool hi_res = false;
};

struct ResultsRankBars {
    std::int32_t x0 = 0, x1 = 0, top_y = 0, band_height = 0;
    // Per difficulty (-1/0/1 indexed as 0/1/2): gradient bands, each a top and bottom color.
    std::array<std::vector<std::array<Rgb, 2>>, 3> bands{};
};

struct ResultsInfoData {
    std::array<ResultsLabelText, static_cast<std::size_t>(ResultsText::Count)> labels{};
    std::array<ResultsValueColumn, static_cast<std::size_t>(ResultsValue::Count)> values{};
    ResultsPercentRows percent_rows{};
    ResultsRankRow rank_tens_only{}, rank_ones_only{}, rank_both_tens{}, rank_both_ones{};
    ResultsRankBars rank_bars{};
    std::int32_t white_color = 0;       // palette id used for most of the sheet
    std::int32_t rank_perfect_color = 0; // palette id once the rank reaches 100
    std::int32_t items_total = 0;       // the "/204" divisor of the Items row
};

Result<ResultsInfoData> parse_results_info(std::string_view json_text);

// Reads menu/results_info.json from the last mod (in load order) that has it,
// else from the gamedata directory. `source` receives the file read.
Result<ResultsInfoData> load_results_info(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string* source = nullptr);

// One frame's statistics, already collected by the game.
struct ResultsInfoInput {
    std::int32_t difficulty = 0;   // -1 easy, 0 normal, 1 hard
    std::int32_t saves = 0;
    std::int32_t continues = 0;
    std::int32_t hours = 0, minutes = 0, seconds = 0;
    std::int32_t walk_km = 0, walk_fraction = 0, run_km = 0, run_fraction = 0;
    std::int32_t items = 0;
    std::int32_t special_items = 0;
    std::int32_t clears = 0;
    std::int32_t ending_bits = 0;  // exactly one of 1,2,4,8,16 picks the ending word
    std::int32_t rank = 0;         // 0..100
    std::int32_t kills_by_shooting = 0;
    std::int32_t kills_by_fighting = 0;
    bool fired_shots = false;
    const char* shot_percent[4] = {};  // rendered "D.DD" percent strings, one per row

    // Palette id per scored row: 7 keeps the row white, 0 marks it as dragging
    // the rank down (the game fills these when it collects the statistics).
    std::int32_t color_ending = 0, color_game_clear = 0, color_saves = 0, color_continues = 0;
    std::int32_t color_total_time = 0, color_items = 0, color_special_items = 0;
    std::int32_t color_kills = 0, color_shooting = 0;
};

enum class ResultsDrawKind : std::uint8_t { Text, Number };

struct ResultsDraw {
    ResultsDrawKind kind = ResultsDrawKind::Text;
    // Text
    ResultsText key = ResultsText::GameResult;
    const char* raw_text = nullptr;  // the shot-percent strings, host-provided
    // Number
    std::int32_t number = 0;
    std::int32_t width_min = 0;
    // Both
    std::int32_t x = 0, y = 0;
    bool positioned = false;
    std::int32_t budget = 0;
    std::int32_t color = 0;
    bool hi_res = false;
    bool align_right = false;
};

// A flat gradient quad (the rank bars).
struct ResultsQuad {
    Point corners[4]{};
    Rgb colors[4]{};
};

struct ResultsInfoLayout {
    std::vector<ResultsDraw> draws;  // replay in order
    std::vector<ResultsQuad> quads;  // after the text, as in the original
};

ResultsInfoLayout layout_results_info(const ResultsInfoData& data, const ResultsInfoInput& in);

}  // namespace sh::inventory
