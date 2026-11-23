// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/inventory/screen_data.h"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "engine/inventory/inventory.h"
#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::inventory {
namespace {

constexpr std::array<std::string_view, 8> kLabelNames = {"Equipment", "Exit",   "Option", "Map",
                                                         "Command",   "Status", "Number", "Name"};
constexpr std::array<std::string_view, 8> kWordNames = {"Use", "Equip", "Unequip", "Reload",
                                                        "Detail", "On",  "Off",     "Look"};

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

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

std::int32_t to_int(const json::Value& v, std::string_view what, std::int32_t lo, std::int32_t hi) {
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail("\"" + std::string(what) + "\" must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
    }
    return static_cast<std::int32_t>(v.integer());
}

std::int32_t int_of(const json::Value& parent, std::string_view key, std::int32_t lo, std::int32_t hi) {
    return to_int(member(parent, key), key, lo, hi);
}

Point point_of(const json::Value& v) {
    if (!v.is_array() || v.array().size() != 2) {
        fail("a point is [x, y]");
    }
    return {to_int(v.array()[0], "x", -4096, 4096), to_int(v.array()[1], "y", -4096, 4096)};
}

InventoryScreenData parse_data(std::string_view json_text) {
    const Result<json::Value> doc = json::parse(json_text);
    if (!doc) {
        fail(doc.error());
    }
    const json::Value& root = doc.value();
    reject_unknown(root, {"labels", "command_words", "command_column", "scroll_arrows"});
    InventoryScreenData data;

    const json::Value& labels = member(root, "labels");
    reject_unknown(labels, {"Equipment", "Exit", "Option", "Map", "Command", "Status", "Number", "Name"});
    // The file's member order is the drawing order.
    std::size_t order = 0;
    for (const auto& [key, spec] : labels.members()) {
        std::size_t index = 0;
        while (kLabelNames[index] != key) {
            index++;
        }
        reject_unknown(spec, {"text", "x", "y", "boxed"});
        const json::Value& text = member(spec, "text");
        const json::Value& boxed = member(spec, "boxed");
        if (!text.is_string() || text.string().empty() || !boxed.is_bool()) {
            fail("label \"" + key + "\" needs a text and \"boxed\"");
        }
        data.labels[index] = {text.string(), {int_of(spec, "x", -4096, 4096), int_of(spec, "y", -4096, 4096)}, boxed.boolean()};
        data.label_order[order++] = static_cast<ScreenLabel>(index);
    }
    if (order != kLabelNames.size()) {
        fail("every label must be present");
    }

    const json::Value& words = member(root, "command_words");
    reject_unknown(words, {"Use", "Equip", "Unequip", "Reload", "Detail", "On", "Off", "Look"});
    for (std::size_t i = 0; i < kWordNames.size(); i++) {
        const json::Value& word = member(words, kWordNames[i]);
        if (!word.is_string() || word.string().empty()) {
            fail("command word must be a non-empty string");
        }
        data.command_words[i] = word.string();
    }

    const json::Value& col = member(root, "command_column");
    reject_unknown(col, {"label_x", "single_row_y", "upper_row_y", "lower_row_y", "row_height", "bar_left",
                         "bar_right", "bar_half", "bar_top_two_rows", "bar_top_one_row", "bar_color"});
    CommandColumnSpec& c = data.column;
    c.label_x = int_of(col, "label_x", -4096, 4096);
    c.single_row_y = int_of(col, "single_row_y", -4096, 4096);
    c.upper_row_y = int_of(col, "upper_row_y", -4096, 4096);
    c.lower_row_y = int_of(col, "lower_row_y", -4096, 4096);
    c.row_height = int_of(col, "row_height", 0, 4096);
    c.bar_left = int_of(col, "bar_left", -4096, 4096);
    c.bar_right = int_of(col, "bar_right", -4096, 4096);
    c.bar_half = int_of(col, "bar_half", 0, 4096);
    c.bar_top_two_rows = int_of(col, "bar_top_two_rows", -4096, 4096);
    c.bar_top_one_row = int_of(col, "bar_top_one_row", -4096, 4096);
    const json::Value& color = member(col, "bar_color");
    if (!color.is_array() || color.array().size() != 3) {
        fail("\"bar_color\" is [r, g, b]");
    }
    c.bar_color = {static_cast<std::uint8_t>(to_int(color.array()[0], "r", 0, 255)),
                   static_cast<std::uint8_t>(to_int(color.array()[1], "g", 0, 255)),
                   static_cast<std::uint8_t>(to_int(color.array()[2], "b", 0, 255))};

    const json::Value& arrows = member(root, "scroll_arrows");
    reject_unknown(arrows, {"triangles", "blink_mask", "blink_step"});
    const json::Value& tris = member(arrows, "triangles");
    if (!tris.is_array() || tris.array().size() != 4) {
        fail("\"triangles\" holds four triangles");
    }
    for (std::size_t t = 0; t < 4; t++) {
        const json::Value& tri = tris.array()[t];
        if (!tri.is_array() || tri.array().size() != 3) {
            fail("a triangle is three points");
        }
        for (std::size_t p = 0; p < 3; p++) {
            data.arrows.triangles[t][p] = point_of(tri.array()[p]);
        }
    }
    data.arrows.blink_mask = int_of(arrows, "blink_mask", 1, 255);
    data.arrows.blink_step = int_of(arrows, "blink_step", 0, 255);
    return data;
}

}  // namespace

const InventoryScreenData& default_screen_data() {
    static const InventoryScreenData kData = [] {
        InventoryScreenData d;
        const ScreenLabelSpec specs[8] = {
            {"Equipment", {118, -80}, true}, {"Exit", {144, 304}, true}, {"Option", {36, 304}, true},
            {"Map", {240, 304}, true},       {"Command", {218, -80}, true}, {"Status", {40, -80}, true},
            {"No.", {16, 184}, false},       {"Name:", {16, 200}, false}};
        for (std::size_t i = 0; i < 8; i++) {
            d.labels[i] = specs[i];
            d.label_order[i] = static_cast<ScreenLabel>(i);
        }
        d.command_words = {"Use", "Equip", "Unequip", "Reload", "Detail", "On", "Off", "Look"};
        d.column = {222, -34, -42, -26, 16, 56, 136, 6, -154, -146, {0, 192, 64}};
        d.arrows.triangles = {{{Point{-60, 12}, Point{-52, 4}, Point{-52, 20}},
                               {Point{-52, 12}, Point{-44, 4}, Point{-44, 20}},
                               {Point{52, 12}, Point{44, 4}, Point{44, 20}},
                               {Point{60, 12}, Point{52, 4}, Point{52, 20}}}};
        d.arrows.blink_mask = 0x1F;
        d.arrows.blink_step = 8;
        return d;
    }();
    return kData;
}

Result<InventoryScreenData> parse_screen_data(std::string_view json_text) try {
    return Result<InventoryScreenData>::ok(parse_data(json_text));
} catch (const std::exception& e) {
    return Result<InventoryScreenData>::fail(e.what());
}

Result<InventoryScreenData> load_screen_data(const std::filesystem::path& gamedata_dir,
                                             const std::filesystem::path& mods_dir, std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "inventory_screen.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<InventoryScreenData>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<InventoryScreenData> parsed = parse_screen_data(text);
    if (!parsed) {
        return Result<InventoryScreenData>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

const std::array<ScreenLabelDraw, 8>& screen_labels() {
    static const std::array<ScreenLabelDraw, 8> kLabels = [] {
        const InventoryScreenData& d = default_screen_data();
        std::array<ScreenLabelDraw, 8> out{};
        for (std::size_t i = 0; i < 8; i++) {
            const ScreenLabel label = d.label_order[i];
            out[i] = {label, d.labels[static_cast<std::size_t>(label)].at, d.labels[static_cast<std::size_t>(label)].boxed};
        }
        return out;
    }();
    return kLabels;
}

CommandColumn layout_command_column(const InventoryScreenData& data, std::uint8_t command, bool on_off_hidden,
                                    bool active, int row) {
    const CommandColumnSpec& s = data.column;
    CommandColumn column;
    column.active = active;
    auto one = [&](CommandLabel label) {
        column.labels[0] = {{s.label_x, s.single_row_y}, label};
        column.label_count = 1;
    };
    auto two = [&](CommandLabel upper, CommandLabel lower) {
        column.labels[0] = {{s.label_x, s.upper_row_y}, upper};
        column.labels[1] = {{s.label_x, s.lower_row_y}, lower};
        column.label_count = 2;
    };
    switch (static_cast<Command>(command)) {
        case Command::OnOff:
            if (!on_off_hidden) {
                two(CommandLabel::On, CommandLabel::Off);
            }
            break;
        case Command::UseHealth:
        case Command::Use:
            one(CommandLabel::Use);
            break;
        case Command::Equip:
            one(CommandLabel::Equip);
            break;
        case Command::Unequip:
            one(CommandLabel::Unequip);
            break;
        case Command::Reload:
            one(CommandLabel::Reload);
            break;
        case Command::UseLook:
            two(CommandLabel::Use, CommandLabel::Look);
            break;
        case Command::EquipReload:
            two(CommandLabel::Equip, CommandLabel::Reload);
            break;
        case Command::UnequipReload:
            two(CommandLabel::Unequip, CommandLabel::Reload);
            break;
        case Command::Look:
            one(CommandLabel::Look);
            break;
        default:
            break;
    }

    if (active) {
        const bool two_rows = command_rows(command) == 2;
        const std::int32_t top =
            (two_rows ? s.bar_top_two_rows : s.bar_top_one_row) + static_cast<std::uint16_t>(row) * s.row_height;
        const Rgb black{0, 0, 0};
        for (int half = 0; half < 2; half++) {
            GradientQuad& quad = column.bar[half];
            const std::int32_t y0 = top + half * s.bar_half;
            const std::int32_t y1 = top + (half + 1) * s.bar_half;
            quad.corners = {Point{s.bar_left, y0}, Point{s.bar_left, y1}, Point{s.bar_right, y0}, Point{s.bar_right, y1}};
            // The bar is green in the middle and fades to black above and below.
            quad.colors = half == 0 ? std::array<Rgb, 4>{black, s.bar_color, black, s.bar_color}
                                    : std::array<Rgb, 4>{s.bar_color, black, s.bar_color, black};
        }
    }
    return column;
}

std::array<GradientTriangle, 4> scroll_arrows(const InventoryScreenData& data, std::int32_t frame) {
    // A blink: the tip fades out while the tail fades in.
    const std::int32_t phase = static_cast<std::int8_t>(frame & data.arrows.blink_mask);
    const auto tip = static_cast<std::uint8_t>(255 - phase * data.arrows.blink_step);
    const auto tail = static_cast<std::uint8_t>(phase * data.arrows.blink_step);
    std::array<GradientTriangle, 4> arrows{};
    for (std::size_t i = 0; i < arrows.size(); i++) {
        arrows[i].corners = data.arrows.triangles[i];
        arrows[i].colors = {Rgb{tip, tip, tip}, Rgb{tail, tail, tail}, Rgb{tail, tail, tail}};
    }
    return arrows;
}

}  // namespace sh::inventory
