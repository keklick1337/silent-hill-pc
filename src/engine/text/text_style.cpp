// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/menu/text_style.json reader.

#include "engine/text/text_style.h"

#include <initializer_list>
#include <stdexcept>

#include "engine/mods/data_merge.h"
#include "formats/common/json.h"

namespace sh::text {

namespace {

using json::Value;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string at(const Value& v, std::string_view what) {
    return "line " + std::to_string(v.line()) + ", column " + std::to_string(v.column()) + ": " + std::string(what);
}

const Value& member(const Value& parent, std::string_view key) {
    const Value* v = parent.find(key);
    if (v == nullptr) {
        fail(at(parent, "missing \"" + std::string(key) + "\""));
    }
    return *v;
}

std::int32_t integer_of(const Value& parent, std::string_view key, std::int64_t lo, std::int64_t hi) {
    const Value& v = member(parent, key);
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, "\"" + std::string(key) + "\" must be an integer in " + std::to_string(lo) + ".." +
                       std::to_string(hi)));
    }
    return static_cast<std::int32_t>(v.integer());
}

void reject_unknown(const Value& object, std::string_view where, std::initializer_list<std::string_view> known) {
    if (!object.is_object()) {
        fail(at(object, "\"" + std::string(where) + "\" must be an object"));
    }
    for (const auto& [key, value] : object.members()) {
        bool found = key == "description";
        for (std::string_view k : known) {
            found = found || key == k;
        }
        if (!found) {
            fail(at(value, "unknown key \"" + key + "\" in \"" + std::string(where) + "\""));
        }
    }
}

template <std::size_t N>
void reject_unknown_names(const Value& object, std::string_view where, const std::string_view (&names)[N]) {
    if (!object.is_object()) {
        fail(at(object, "\"" + std::string(where) + "\" must be an object"));
    }
    for (const auto& [key, value] : object.members()) {
        bool found = key == "description";
        for (std::string_view n : names) {
            found = found || key == n;
        }
        if (!found) {
            fail(at(value, "unknown key \"" + key + "\" in \"" + std::string(where) + "\""));
        }
    }
}

std::uint32_t color_of(const Value& v) {
    if (!v.is_array() || v.array().size() != 3) {
        fail(at(v, "a colour is [r, g, b]"));
    }
    std::uint32_t rgb = 0;
    for (std::size_t i = 0; i < 3; i++) {
        const Value& c = v.array()[i];
        if (!c.is_integer() || c.integer() < 0 || c.integer() > 255) {
            fail(at(c, "colour components are 0..255"));
        }
        rgb |= static_cast<std::uint32_t>(c.integer()) << (8 * i);
    }
    return rgb;
}

}  // namespace

Result<TextStyle> parse_text_style(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<TextStyle>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, "root", {"colors", "message"});
    TextStyle s;

    const Value& colors = member(root, "colors");
    reject_unknown_names(colors, "colors", kTextColorNames);
    for (std::size_t i = 0; i < kTextColorCount; i++) {
        s.rgb[i] = color_of(member(colors, kTextColorNames[i]));
    }

    const Value& message = member(root, "message");
    reject_unknown(message, "message", {"color", "line_start_x", "box_y", "display_all_length"});
    const Value& color = member(message, "color");
    s.message_color = -1;
    for (std::size_t i = 0; i < kTextColorCount && color.is_string(); i++) {
        if (color.string() == kTextColorNames[i]) {
            s.message_color = static_cast<std::int32_t>(i);
        }
    }
    if (s.message_color < 0) {
        fail(at(color, "\"color\" must name a colour of \"colors\""));
    }
    s.message_line_start_x = integer_of(message, "line_start_x", -160, 160);
    const Value& box_y = member(message, "box_y");
    reject_unknown_names(box_y, "box_y", kMessageBoxPositionNames);
    for (std::size_t i = 0; i < kMessageBoxPositions; i++) {
        s.message_box_y[i] = integer_of(box_y, kMessageBoxPositionNames[i], -120, 120);
    }
    s.display_all_length = integer_of(message, "display_all_length", 1, 32767);
    return Result<TextStyle>::ok(s);
} catch (const std::exception& e) {
    return Result<TextStyle>::fail(e.what());
}

Result<TextStyle> load_text_style(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                  std::string* source) {
    std::string where;
    const Result<std::string> read =
        mods::read_data_text(gamedata_dir, mods_dir, std::filesystem::path("menu") / "text_style.json", &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<TextStyle>::fail(read.error());
    }
    Result<TextStyle> parsed = parse_text_style(read.value());
    if (!parsed) {
        return Result<TextStyle>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::text
