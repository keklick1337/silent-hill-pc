// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// gamedata/menu/results_info.json reader and the results sheet's layout: the
// texts, positions and bar colors are data; the row composition and the
// right-aligned number columns are this file's arithmetic, kept in the
// original screen's draw order.

#include "engine/inventory/results_info.h"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::inventory {
namespace {

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error(what);
}

const json::Value& member(const json::Value& parent, std::string_view key) {
    const json::Value* v = parent.find(key);
    if (v == nullptr) {
        fail("missing key \"" + std::string(key) + "\"");
    }
    return *v;
}

void reject_unknown(const json::Value& parent, std::initializer_list<std::string_view> known) {
    if (!parent.is_object()) {
        fail("expected an object");
    }
    for (const auto& [key, value] : parent.members()) {
        bool found = false;
        for (std::string_view k : known) {
            found = found || key == k;
        }
        if (!found) {
            fail("unknown key \"" + key + "\"");
        }
    }
}

std::int32_t int_of(const json::Value& parent, std::string_view key, std::int32_t lo, std::int32_t hi) {
    const json::Value& v = member(parent, key);
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail("\"" + std::string(key) + "\" must be an integer in " + std::to_string(lo) + ".." +
             std::to_string(hi));
    }
    return static_cast<std::int32_t>(v.integer());
}

bool bool_of(const json::Value& parent, std::string_view key) {
    const json::Value& v = member(parent, key);
    if (!v.is_bool()) {
        fail("\"" + std::string(key) + "\" must be true or false");
    }
    return v.boolean();
}

Rgb rgb_of(const json::Value& v) {
    if (!v.is_array() || v.array().size() != 3) {
        fail("a color is [r, g, b]");
    }
    Rgb rgb{};
    std::uint8_t* parts[3] = {&rgb.r, &rgb.g, &rgb.b};
    for (std::size_t i = 0; i < 3; i++) {
        const json::Value& c = v.array()[i];
        if (!c.is_integer() || c.integer() < 0 || c.integer() > 255) {
            fail("color components are 0..255");
        }
        *parts[i] = static_cast<std::uint8_t>(c.integer());
    }
    return rgb;
}

struct TextKeyInfo {
    const char* key;
    ResultsText id;
};
constexpr std::array<TextKeyInfo, static_cast<std::size_t>(ResultsText::Count)> kTextKeys{{
    {"game_result", ResultsText::GameResult},
    {"mode", ResultsText::Mode},
    {"difficulty_easy", ResultsText::DifficultyEasy},
    {"difficulty_normal", ResultsText::DifficultyNormal},
    {"difficulty_hard", ResultsText::DifficultyHard},
    {"saves", ResultsText::Saves},
    {"continues", ResultsText::Continues},
    {"total_time", ResultsText::TotalTime},
    {"hours_unit", ResultsText::HoursUnit},
    {"minutes_unit", ResultsText::MinutesUnit},
    {"seconds_unit", ResultsText::SecondsUnit},
    {"decimal_point", ResultsText::DecimalPoint},
    {"km_unit", ResultsText::KmUnit},
    {"walking_distance", ResultsText::WalkingDistance},
    {"running_distance", ResultsText::RunningDistance},
    {"items", ResultsText::Items},
    {"slash", ResultsText::Slash},
    {"plus", ResultsText::Plus},
    {"game_clear", ResultsText::GameClear},
    {"ending", ResultsText::Ending},
    {"ending_good_plus", ResultsText::EndingGoodPlus},
    {"ending_good", ResultsText::EndingGood},
    {"ending_bad_plus", ResultsText::EndingBadPlus},
    {"ending_bad", ResultsText::EndingBad},
    {"ending_ufo", ResultsText::EndingUfo},
    {"your_rank", ResultsText::YourRank},
    {"star", ResultsText::Star},
    {"defeated_by_shooting", ResultsText::DefeatedByShooting},
    {"defeated_by_fighting", ResultsText::DefeatedByFighting},
    {"shooting_style", ResultsText::ShootingStyle},
    {"short_range_shots", ResultsText::ShortRangeShots},
    {"middle_range_shots", ResultsText::MiddleRangeShots},
    {"long_range_shots", ResultsText::LongRangeShots},
    {"no_aiming_shots", ResultsText::NoAimingShots},
    {"paren_left", ResultsText::ParenLeft},
    {"paren_right", ResultsText::ParenRight},
}};

struct ValueKeyInfo {
    const char* key;
    ResultsValue id;
};
constexpr std::array<ValueKeyInfo, static_cast<std::size_t>(ResultsValue::Count)> kValueKeys{{
    {"saves", ResultsValue::Saves},
    {"continues", ResultsValue::Continues},
    {"hours", ResultsValue::Hours},
    {"minutes", ResultsValue::Minutes},
    {"seconds", ResultsValue::Seconds},
    {"walk_km", ResultsValue::WalkKm},
    {"run_km", ResultsValue::RunKm},
    {"items", ResultsValue::Items},
    {"game_clear", ResultsValue::GameClear},
    {"kills_shooting", ResultsValue::KillsShooting},
    {"kills_fighting", ResultsValue::KillsFighting},
}};

ResultsText text_key_of(const std::string& key, std::int32_t line) {
    for (const TextKeyInfo& info : kTextKeys) {
        if (key == info.key) {
            return info.id;
        }
    }
    fail("line " + std::to_string(line) + ": unknown label \"" + key + "\"");
}

ResultsLabelText parse_label(const json::Value& v) {
    reject_unknown(v, {"text", "x", "y", "budget", "align_right"});
    ResultsLabelText out;
    const json::Value& text = member(v, "text");
    if (!text.is_string() || text.string().empty()) {
        fail("\"text\" must be a non-empty string");
    }
    out.text = text.string();
    const bool has_x = v.find("x") != nullptr;
    const bool has_y = v.find("y") != nullptr;
    if (has_x != has_y) {
        fail("a label has either both \"x\" and \"y\" or neither");
    }
    out.positioned = has_x;
    if (out.positioned) {
        out.x = int_of(v, "x", -1024, 1024);
        out.y = int_of(v, "y", -1024, 1024);
    }
    out.budget = int_of(v, "budget", 0, 255);
    if (v.find("align_right") != nullptr) {
        out.align_right = bool_of(v, "align_right");
    }
    return out;
}

ResultsValueColumn parse_value_column(const json::Value& v) {
    reject_unknown(v, {"anchor_x", "digit_span", "span", "width_min"});
    const json::Value& span = member(v, "span");
    if (!span.is_string() || (span.string() != "digits" && span.string() != "digits_minus_one")) {
        fail("\"span\" is \"digits\" or \"digits_minus_one\"");
    }
    ResultsValueColumn out;
    out.anchor_x = int_of(v, "anchor_x", -1024, 1024);
    out.digit_span = int_of(v, "digit_span", 0, 64);
    out.span_by_count = span.string() == "digits";
    out.width_min = int_of(v, "width_min", 0, 15);
    return out;
}

ResultsRankRow parse_rank_row(const json::Value& v) {
    reject_unknown(v, {"x", "y", "budget", "hi_res"});
    ResultsRankRow out;
    out.x = int_of(v, "x", -1024, 1024);
    out.y = int_of(v, "y", -1024, 1024);
    out.budget = int_of(v, "budget", 0, 255);
    out.hi_res = bool_of(v, "hi_res");
    return out;
}

std::vector<std::array<Rgb, 2>> parse_bar_bands(const json::Value& v) {
    if (!v.is_array() || v.array().empty()) {
        fail("a difficulty's \"rank_bars\" list its bands");
    }
    std::vector<std::array<Rgb, 2>> bands;
    for (const json::Value& band : v.array()) {
        reject_unknown(band, {"top", "bottom"});
        bands.push_back({rgb_of(member(band, "top")), rgb_of(member(band, "bottom"))});
    }
    return bands;
}

}  // namespace

Result<ResultsInfoData> parse_results_info(std::string_view json_text) try {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<ResultsInfoData>::fail(doc.error());
    }
    const json::Value& root = doc.value();
    reject_unknown(root, {"colors", "labels", "values", "percent_rows", "rank", "rank_bars", "items_total"});
    ResultsInfoData data;

    const json::Value& colors = member(root, "colors");
    reject_unknown(colors, {"white", "rank_perfect"});
    data.white_color = int_of(colors, "white", 0, 255);
    data.rank_perfect_color = int_of(colors, "rank_perfect", 0, 255);

    const json::Value& labels = member(root, "labels");
    if (!labels.is_object()) {
        fail("\"labels\" must be an object");
    }
    std::array<bool, static_cast<std::size_t>(ResultsText::Count)> have_label{};
    for (const auto& [key, value] : labels.members()) {
        const ResultsText id = text_key_of(key, static_cast<std::int32_t>(value.line()));
        data.labels[static_cast<std::size_t>(id)] = parse_label(value);
        have_label[static_cast<std::size_t>(id)] = true;
    }
    for (bool have : have_label) {
        if (!have) {
            fail("\"labels\" must list every label of the sheet");
        }
    }

    const json::Value& values = member(root, "values");
    if (!values.is_object()) {
        fail("\"values\" must be an object");
    }
    std::array<bool, static_cast<std::size_t>(ResultsValue::Count)> have_value{};
    for (const auto& [key, value] : values.members()) {
        const ValueKeyInfo* info = nullptr;
        for (const ValueKeyInfo& candidate : kValueKeys) {
            if (key == candidate.key) {
                info = &candidate;
            }
        }
        if (info == nullptr) {
            fail("unknown value column \"" + key + "\"");
        }
        data.values[static_cast<std::size_t>(info->id)] = parse_value_column(value);
        have_value[static_cast<std::size_t>(info->id)] = true;
    }
    for (bool have : have_value) {
        if (!have) {
            fail("\"values\" must list every number column of the sheet");
        }
    }

    const json::Value& rows = member(root, "percent_rows");
    reject_unknown(rows, {"value_x", "first_value_y", "row_pitch", "value_budget", "rows"});
    const json::Value& row_list = member(rows, "rows");
    if (!row_list.is_array() || row_list.array().size() != 4) {
        fail("\"percent_rows.rows\" lists its four rows");
    }
    for (std::size_t i = 0; i < 4; i++) {
        const json::Value& label = row_list.array()[i];
        if (!label.is_string()) {
            fail("\"percent_rows.rows\" entries are label names");
        }
        data.percent_rows.labels[i] = text_key_of(label.string(), static_cast<std::int32_t>(label.line()));
    }
    data.percent_rows.value_x = int_of(rows, "value_x", -1024, 1024);
    data.percent_rows.first_value_y = int_of(rows, "first_value_y", -1024, 1024);
    data.percent_rows.row_pitch = int_of(rows, "row_pitch", -256, 256);
    data.percent_rows.value_budget = int_of(rows, "value_budget", 0, 255);

    const json::Value& rank = member(root, "rank");
    reject_unknown(rank, {"tens_only", "ones_only", "both_tens", "both_ones"});
    data.rank_tens_only = parse_rank_row(member(rank, "tens_only"));
    data.rank_ones_only = parse_rank_row(member(rank, "ones_only"));
    data.rank_both_tens = parse_rank_row(member(rank, "both_tens"));
    data.rank_both_ones = parse_rank_row(member(rank, "both_ones"));

    const json::Value& bars = member(root, "rank_bars");
    reject_unknown(bars, {"left_x", "right_x", "top_y", "band_height", "easy", "normal", "hard"});
    data.rank_bars.x0 = int_of(bars, "left_x", -1024, 1024);
    data.rank_bars.x1 = int_of(bars, "right_x", -1024, 1024);
    data.rank_bars.top_y = int_of(bars, "top_y", -1024, 1024);
    data.rank_bars.band_height = int_of(bars, "band_height", 0, 64);
    data.rank_bars.bands[0] = parse_bar_bands(member(bars, "easy"));
    data.rank_bars.bands[1] = parse_bar_bands(member(bars, "normal"));
    data.rank_bars.bands[2] = parse_bar_bands(member(bars, "hard"));

    data.items_total = int_of(root, "items_total", 0, 9999);

    return Result<ResultsInfoData>::ok(data);
} catch (const std::exception& e) {
    return Result<ResultsInfoData>::fail(e.what());
}

Result<ResultsInfoData> load_results_info(const std::filesystem::path& gamedata_dir,
                                          const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "results_info.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<ResultsInfoData>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<ResultsInfoData> parsed = parse_results_info(text);
    if (!parsed) {
        return Result<ResultsInfoData>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

namespace {

// The original counts a value's decimal digits with an unsigned divide loop.
std::int32_t digit_count(std::int32_t value) {
    std::uint32_t n = static_cast<std::uint32_t>(value);
    std::int32_t digits = 1;
    while (n >= 10) {
        n /= 10;
        digits++;
    }
    return digits;
}

}  // namespace

ResultsInfoLayout layout_results_info(const ResultsInfoData& data, const ResultsInfoInput& in) {
    ResultsInfoLayout out;
    const std::int32_t white = data.white_color;

    auto push_text = [&](ResultsText key, std::int32_t color) {
        const ResultsLabelText& t = data.labels[static_cast<std::size_t>(key)];
        ResultsDraw r;
        r.kind = ResultsDrawKind::Text;
        r.key = key;
        r.x = t.x;
        r.y = t.y;
        r.positioned = t.positioned;
        r.budget = t.budget;
        r.color = color;
        r.align_right = t.align_right;
        out.draws.push_back(r);
    };

    auto push_percent = [&](const char* text, std::int32_t x, std::int32_t y, std::int32_t color) {
        ResultsDraw r;
        r.kind = ResultsDrawKind::Text;
        r.raw_text = text;
        r.x = x;
        r.y = y;
        r.positioned = true;
        r.budget = data.percent_rows.value_budget;
        r.color = color;
        out.draws.push_back(r);
    };

    auto push_number = [&](ResultsValue key, std::int32_t value, std::int32_t color, std::int32_t y) {
        const ResultsValueColumn& c = data.values[static_cast<std::size_t>(key)];
        const std::int32_t digits = digit_count(value);
        const std::int32_t spanned = c.span_by_count ? digits : digits - 1;
        ResultsDraw r;
        r.kind = ResultsDrawKind::Number;
        r.number = value;
        r.width_min = c.width_min;
        r.x = c.anchor_x - spanned * c.digit_span;
        r.y = y;
        r.positioned = true;
        r.color = color;
        out.draws.push_back(r);
    };

    // A number drawn at the running cursor (the km fractions keep writing where
    // the previous draw stopped, like the original).
    auto push_cursor_number = [&](std::int32_t value, std::int32_t width_min, std::int32_t color) {
        ResultsDraw r;
        r.kind = ResultsDrawKind::Number;
        r.number = value;
        r.width_min = width_min;
        r.color = color;
        out.draws.push_back(r);
    };

    auto push_stars = [&](const ResultsRankRow& row, std::int32_t count, std::int32_t color) {
        for (std::int32_t i = 0; i < count; i++) {
            ResultsDraw r;
            r.kind = ResultsDrawKind::Text;
            r.key = ResultsText::Star;
            r.x = row.x;
            r.y = row.y;
            r.positioned = true;
            r.budget = row.budget;
            r.color = color;
            r.hi_res = row.hi_res;
            out.draws.push_back(r);
        }
    };

    push_text(ResultsText::GameResult, white);
    push_text(ResultsText::Mode, white);
    switch (in.difficulty) {
        case -1: push_text(ResultsText::DifficultyEasy, white); break;
        case 0: push_text(ResultsText::DifficultyNormal, white); break;
        case 1: push_text(ResultsText::DifficultyHard, white); break;
        default: break;
    }

    push_text(ResultsText::Saves, white);
    push_number(ResultsValue::Saves, in.saves, in.color_saves, 7);
    push_text(ResultsText::Continues, white);
    push_number(ResultsValue::Continues, in.continues, in.color_continues, 28);

    // The whole time row (units included) draws in the time row's color.
    push_text(ResultsText::TotalTime, white);
    push_number(ResultsValue::Hours, in.hours, in.color_total_time, 49);
    push_text(ResultsText::HoursUnit, in.color_total_time);
    push_number(ResultsValue::Minutes, in.minutes, in.color_total_time, 49);
    push_text(ResultsText::MinutesUnit, in.color_total_time);
    push_number(ResultsValue::Seconds, in.seconds, in.color_total_time, 49);
    push_text(ResultsText::SecondsUnit, in.color_total_time);

    // Distances: whole kilometers right-aligned, then "." and the fraction
    // zero-padded to three digits at the running cursor.
    push_text(ResultsText::WalkingDistance, white);
    push_number(ResultsValue::WalkKm, in.walk_km, white, 70);
    push_text(ResultsText::DecimalPoint, white);
    if (in.walk_fraction < 100) {
        push_cursor_number(0, 1, white);
    }
    if (in.walk_fraction < 10) {
        push_cursor_number(0, 1, white);
    }
    push_cursor_number(in.walk_fraction, 3, white);
    push_text(ResultsText::KmUnit, white);

    push_text(ResultsText::RunningDistance, white);
    push_number(ResultsValue::RunKm, in.run_km, white, 91);
    push_text(ResultsText::DecimalPoint, white);
    if (in.run_fraction < 100) {
        push_cursor_number(0, 1, white);
    }
    if (in.run_fraction < 10) {
        push_cursor_number(0, 1, white);
    }
    push_cursor_number(in.run_fraction, 3, white);
    push_text(ResultsText::KmUnit, white);

    push_text(ResultsText::Items, white);
    push_number(ResultsValue::Items, in.items, in.color_items, 112);
    push_text(ResultsText::Slash, in.color_items);
    push_cursor_number(data.items_total, 3, in.color_items);
    push_text(ResultsText::ParenLeft, in.color_special_items);
    if (in.special_items != 0) {
        push_text(ResultsText::Plus, in.color_special_items);
        push_cursor_number(in.special_items, 1, in.color_special_items);
    }
    push_text(ResultsText::ParenRight, in.color_special_items);

    push_text(ResultsText::GameClear, white);
    push_number(ResultsValue::GameClear, in.clears, in.color_game_clear, -35);
    push_text(ResultsText::Ending, white);
    switch (in.ending_bits & 0x1F) {
        case 1: push_text(ResultsText::EndingGoodPlus, in.color_ending); break;
        case 2: push_text(ResultsText::EndingGood, in.color_ending); break;
        case 4: push_text(ResultsText::EndingBadPlus, in.color_ending); break;
        case 8: push_text(ResultsText::EndingBad, in.color_ending); break;
        case 16: push_text(ResultsText::EndingUfo, in.color_ending); break;
        default: break;
    }

    push_text(ResultsText::DefeatedByShooting, white);
    push_number(ResultsValue::KillsShooting, in.kills_by_shooting, in.color_kills, 154);
    push_text(ResultsText::DefeatedByFighting, white);
    push_number(ResultsValue::KillsFighting, in.kills_by_fighting, in.color_kills, 133);

    if (in.fired_shots) {
        push_text(ResultsText::ShootingStyle, white);
        for (std::size_t i = 0; i < 4; i++) {
            push_text(data.percent_rows.labels[i], white);
            push_percent(in.shot_percent[i], data.percent_rows.value_x,
                         data.percent_rows.first_value_y + static_cast<std::int32_t>(i) * data.percent_rows.row_pitch,
                         in.color_shooting);
        }
    }

    push_text(ResultsText::YourRank, white);

    // The rank as star rows: a multiple of ten fills the tens row alone, a
    // sub-ten rank the ones row alone, anything else splits across both.
    const std::int32_t star_color = in.rank >= 100 ? data.rank_perfect_color : white;
    const std::int32_t tens = in.rank / 10;
    const std::int32_t ones = in.rank % 10;
    if (tens != 0 && ones == 0) {
        push_stars(data.rank_tens_only, tens, star_color);
    } else if (tens == 0 && ones != 0) {
        push_stars(data.rank_ones_only, ones, star_color);
    } else {
        push_stars(data.rank_both_tens, tens, star_color);
        push_stars(data.rank_both_ones, ones, star_color);
    }

    // The gradient bars under the title; a save with an unknown difficulty
    // drew them with uninitialised colors, which valid saves never produce.
    if (in.difficulty >= -1 && in.difficulty <= 1) {
        const std::size_t index = static_cast<std::size_t>(in.difficulty + 1);
        const std::vector<std::array<Rgb, 2>>& bands = data.rank_bars.bands[index];
        for (std::size_t i = 0; i < bands.size(); i++) {
            const std::int32_t y0 = data.rank_bars.top_y + static_cast<std::int32_t>(i) * data.rank_bars.band_height;
            const std::int32_t y1 = y0 + data.rank_bars.band_height;
            ResultsQuad q;
            q.corners[0] = Point{data.rank_bars.x0, y0};
            q.corners[1] = Point{data.rank_bars.x0, y1};
            q.corners[2] = Point{data.rank_bars.x1, y0};
            q.corners[3] = Point{data.rank_bars.x1, y1};
            q.colors[0] = bands[i][0];
            q.colors[1] = bands[i][1];
            q.colors[2] = bands[i][0];
            q.colors[3] = bands[i][1];
            out.quads.push_back(q);
        }
    }

    return out;
}

}  // namespace sh::inventory
