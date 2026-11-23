// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/menu/staff_roll.json reader and the roll markup encoder.

#include "engine/screens/staff_roll_data.h"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <vector>

#include "engine/mods/data_merge.h"
#include "engine/screens/roll_math.h"
#include "formats/common/json.h"

namespace sh::screens {

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

std::int64_t integer_of(const Value& parent, std::string_view key, std::int64_t lo, std::int64_t hi) {
    const Value& v = member(parent, key);
    if (!v.is_integer() || v.integer() < lo || v.integer() > hi) {
        fail(at(v, "\"" + std::string(key) + "\" must be an integer in " + std::to_string(lo) + ".." +
                       std::to_string(hi)));
    }
    return v.integer();
}

float real_of(const Value& v, std::string_view what) {
    if (!v.is_number()) {
        fail(at(v, std::string(what) + " must be a number"));
    }
    return static_cast<float>(v.number());
}

std::uint32_t color_of(const Value& v) {
    if (!v.is_array() || v.array().size() != 3) {
        fail(at(v, "a color is [r, g, b]"));
    }
    std::uint32_t rgb = 0;
    for (std::size_t i = 0; i < 3; i++) {
        const Value& c = v.array()[i];
        if (!c.is_integer() || c.integer() < 0 || c.integer() > 255) {
            fail(at(c, "color components are 0..255"));
        }
        rgb |= static_cast<std::uint32_t>(c.integer()) << (8 * i);
    }
    return rgb;
}

const std::string& string_of(const Value& parent, std::string_view key) {
    const Value& v = member(parent, key);
    if (!v.is_string()) {
        fail(at(v, "\"" + std::string(key) + "\" must be a string"));
    }
    return v.string();
}

void reject_unknown(const Value& object, std::initializer_list<std::string_view> known) {
    for (const auto& [key, value] : object.members()) {
        bool found = key.rfind("_", 0) == 0 || key == "description";  // notes for people
        for (std::string_view k : known) {
            found = found || key == k;
        }
        if (!found) {
            fail(at(value, "unknown key \"" + key + "\""));
        }
    }
}

// The glyph string of a logo set: every char one atlas glyph (code points up to 0x7F).
template <std::size_t N>
void glyphs_of(const Value& parent, std::string_view key, std::array<std::uint8_t, N>& out) {
    const std::string& s = string_of(parent, key);
    if (s.size() != N) {
        fail(at(member(parent, key), "\"" + std::string(key) + "\" must have " + std::to_string(N) + " characters"));
    }
    for (std::size_t i = 0; i < N; i++) {
        out[i] = static_cast<std::uint8_t>(s[i]);
    }
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// "+N" / "-N" with N a power of two up to 128 -> 0x10..0x1F.
int move_code(std::string_view tag) {
    const bool back = tag[0] == '-';
    int n = 0;
    for (char c : tag.substr(1)) {
        if (c < '0' || c > '9') return -1;
        n = n * 10 + (c - '0');
        if (n > 128) return -1;
    }
    for (int bit = 0; bit < 8; bit++) {
        if (n == (1 << bit)) {
            return (back ? 0x18 : 0x10) + bit;
        }
    }
    return -1;
}

}  // namespace

Result<std::string> encode_roll_markup(std::string_view markup, const RollFont& font) {
    struct Named {
        std::string_view name;
        int base;
    };
    static constexpr Named kNamed[] = {
        {"span", roll_code::kSpanFirst},         {"/span", roll_code::kSpanEnd},
        {"fwd_half", roll_code::kForwardHalf},   {"fwd_full", roll_code::kForwardFull},
        {"back_half", roll_code::kBackHalf},     {"back_full", roll_code::kBackFull},
    };
    std::string out;
    bool logo = false;
    for (std::size_t i = 0; i < markup.size(); i++) {
        const char ch = markup[i];
        if (ch == '}') {
            return Result<std::string>::fail("unmatched '}' at " + std::to_string(i));
        }
        if (ch != '{') {
            const auto byte = static_cast<unsigned char>(ch);
            if (byte != '\n' && (byte < 0x20 || byte > 0x7E)) {
                return Result<std::string>::fail("character " + std::to_string(byte) + " at " + std::to_string(i) +
                                                 " needs {xNN}");
            }
            if (logo && byte != '\n') {
                int code = -1;
                for (std::size_t k = 0; k < font.logo_glyphs.size() && code < 0; k++) {
                    if (font.logo_glyphs[k] == byte) code = roll_code::kLogoFirst + static_cast<int>(k);
                }
                for (std::size_t k = 0; k < font.logo_glyphs_2.size() && code < 0; k++) {
                    if (font.logo_glyphs_2[k] == byte) code = roll_code::kLogo2First + static_cast<int>(k);
                }
                if (code < 0) {
                    return Result<std::string>::fail(std::string("'") + ch + "' has no logo glyph");
                }
                out.push_back(static_cast<char>(code));
            } else {
                out.push_back(ch);
            }
            continue;
        }
        const std::size_t close = markup.find('}', i);
        if (close == std::string_view::npos) {
            return Result<std::string>::fail("unclosed '{' at " + std::to_string(i));
        }
        const std::string_view tag = markup.substr(i + 1, close - i - 1);
        i = close;
        int code = -1;
        if (tag == "kcet" || tag == "/kcet") {
            logo = tag == "kcet";
            continue;
        }
        if (tag.size() == 2 && tag[0] == 'c' && tag[1] >= '1' && tag[1] <= '7') {
            code = tag[1] - '0';
        } else if (tag.size() == 3 && tag[0] == 'x' && hex_digit(tag[1]) >= 0 && hex_digit(tag[2]) >= 0) {
            code = hex_digit(tag[1]) * 16 + hex_digit(tag[2]);
            if (code == 0) {
                return Result<std::string>::fail("{x00} would end the line");
            }
        } else if (!tag.empty() && (tag[0] == '+' || tag[0] == '-')) {
            code = move_code(tag);
        } else {
            for (const Named& n : kNamed) {
                if (tag.substr(0, n.name.size()) != n.name) continue;
                const std::string_view rest = tag.substr(n.name.size());
                if (rest.empty()) {
                    code = n.base;
                } else if (rest.size() == 1 && rest[0] >= '1' && rest[0] <= '7') {
                    code = n.base + (rest[0] - '0');
                }
                break;
            }
        }
        if (code < 0) {
            return Result<std::string>::fail("unknown tag {" + std::string(tag) + "}");
        }
        out.push_back(static_cast<char>(code));
    }
    // A span measure runs to its closing code; the encoding keeps every span closed on
    // its line so the measure never needs the end of the string.
    for (std::size_t i = 0; i < out.size(); i++) {
        const auto c = static_cast<unsigned char>(out[i]);
        if (c < roll_code::kSpanFirst || c >= roll_code::kSpanEnd) continue;
        bool closed = false;
        for (std::size_t k = i + 1; k < out.size() && !closed; k++) {
            const auto n = static_cast<unsigned char>(out[k]);
            closed = n == c + roll_code::kSpanCloseOffset || n == c || n == roll_code::kNewline ||
                     n == roll_code::kReturn || n == roll_code::kHome;
        }
        if (!closed) {
            return Result<std::string>::fail("span at " + std::to_string(i) + " is not closed");
        }
    }
    return Result<std::string>::ok(std::move(out));
}

Result<StaffRollData> parse_staff_roll(std::string_view json_text) try {
    const Result<Value> doc = json::parse(json_text);
    if (!doc) {
        return Result<StaffRollData>::fail(doc.error());
    }
    const Value& root = doc.value();
    reject_unknown(root, {"endings", "font", "flat", "perspective", "logo", "lines"});
    StaffRollData d;

    // Keyed by ending name so a mod patch can change one ending's music or timing.
    const Value& endings = member(root, "endings");
    if (!endings.is_object()) {
        fail(at(endings, "\"endings\" must be an object keyed by ending name"));
    }
    for (const auto& [key, value] : endings.members()) {
        bool known = key == "description";
        for (std::string_view name : kEndingNames) {
            known = known || key == name;
        }
        if (!known) {
            fail(at(value, "unknown ending \"" + key + "\""));
        }
    }
    for (std::size_t i = 0; i < kEndingCount; i++) {
        const Value& e = member(endings, kEndingNames[i]);
        reject_unknown(e, {"music", "music_start", "length"});
        EndingRoll& out = d.endings[i];
        out.music = string_of(e, "music");
        if (out.music.empty()) {
            fail(at(member(e, "music"), "\"music\" must name a sound"));
        }
        out.timing.start = static_cast<std::int32_t>(integer_of(e, "music_start", -32768, 32767));
        out.timing.length = static_cast<std::int32_t>(integer_of(e, "length", 1, 32767));
    }

    const Value& font = member(root, "font");
    reject_unknown(font, {"advance", "logo_glyphs", "logo_glyphs_2", "colors"});
    const Value& advance = member(font, "advance");
    if (!advance.is_array() || advance.array().size() != d.font.advance.size()) {
        fail(at(advance, "\"advance\" must list 256 advances"));
    }
    for (std::size_t i = 0; i < d.font.advance.size(); i++) {
        const Value& a = advance.array()[i];
        if (!a.is_integer() || a.integer() < -32768 || a.integer() > 32767) {
            fail(at(a, "advances are 16-bit integers"));
        }
        d.font.advance[i] = static_cast<std::int16_t>(a.integer());
    }
    glyphs_of(font, "logo_glyphs", d.font.logo_glyphs);
    glyphs_of(font, "logo_glyphs_2", d.font.logo_glyphs_2);
    const Value& colors = member(font, "colors");
    if (!colors.is_array() || colors.array().size() != d.font.colors.size()) {
        fail(at(colors, "\"colors\" must list 7 colors"));
    }
    for (std::size_t i = 0; i < d.font.colors.size(); i++) {
        d.font.colors[i] = color_of(colors.array()[i]);
    }

    const Value& flat = member(root, "flat");
    reject_unknown(flat, {"shadow_color", "shadow_blend", "text_color"});
    d.style.shadow_rgb = color_of(member(flat, "shadow_color"));
    d.style.shadow_blend = static_cast<std::uint32_t>(integer_of(flat, "shadow_blend", 0, 3));
    d.style.text_rgb = color_of(member(flat, "text_color"));

    const Value& persp = member(root, "perspective");
    reject_unknown(persp, {"flat_color", "focal", "scale", "origin", "column_step", "row_step_q12", "logo"});
    d.style.perspective_flat_rgb = color_of(member(persp, "flat_color"));
    RollPlane& plane = d.style.plane;
    plane.focal = static_cast<std::int32_t>(integer_of(persp, "focal", 1, 511));
    plane.scale = roll_math::q12_from_float(real_of(member(persp, "scale"), "\"scale\""));
    const Value& origin = member(persp, "origin");
    if (!origin.is_array() || origin.array().size() != 3) {
        fail(at(origin, "\"origin\" is [x, y, z]"));
    }
    plane.origin_x = roll_math::q12_from_float(real_of(origin.array()[0], "origin x"));
    plane.origin_y = roll_math::q12_from_float(real_of(origin.array()[1], "origin y"));
    plane.origin_z = roll_math::q12_from_float(real_of(origin.array()[2], "origin z"));
    plane.column_x = roll_math::q12_from_float(real_of(member(persp, "column_step"), "\"column_step\""));
    const Value& row = member(persp, "row_step_q12");
    if (!row.is_array() || row.array().size() != 2 || !row.array()[0].is_integer() || !row.array()[1].is_integer()) {
        fail(at(row, "\"row_step_q12\" is [y, z] in Q.12 integers"));
    }
    plane.row_y = static_cast<std::int32_t>(row.array()[0].integer());
    plane.row_z = static_cast<std::int32_t>(row.array()[1].integer());

    const Value& logo = member(persp, "logo");
    reject_unknown(logo, {"tilt_degrees", "spin_ticks", "approach_ticks", "approach_from", "approach_to",
                        "hold_until", "retreat_ticks", "retreat_to"});
    LogoAnimation& a = d.style.logo;
    {
        // A Q.12 turn from degrees, truncated in float and stored in 16 bits like the
        // game's angle constants.
        volatile float turn = real_of(member(logo, "tilt_degrees"), "\"tilt_degrees\"") / 360.0f;
        a.tilt_angle = static_cast<std::int16_t>(roll_math::q12_from_float(turn));
    }
    a.spin_ticks = static_cast<std::int32_t>(integer_of(logo, "spin_ticks", 1, 100000));
    a.approach_ticks = static_cast<std::int32_t>(integer_of(logo, "approach_ticks", 1, 100000));
    a.approach_from = real_of(member(logo, "approach_from"), "\"approach_from\"");
    a.approach_to = real_of(member(logo, "approach_to"), "\"approach_to\"");
    a.hold_until = static_cast<std::int32_t>(integer_of(logo, "hold_until", 0, 100000));
    a.retreat_ticks = static_cast<std::int32_t>(integer_of(logo, "retreat_ticks", 1, 100000));
    a.retreat_to = real_of(member(logo, "retreat_to"), "\"retreat_to\"");
    if (a.hold_until < a.spin_ticks + a.approach_ticks) {
        fail(at(logo, "\"hold_until\" must not be before the approach ends"));
    }

    const Result<std::string> encoded_logo = encode_roll_markup(string_of(root, "logo"), d.font);
    if (!encoded_logo) {
        fail(at(member(root, "logo"), "logo: " + encoded_logo.error()));
    }
    d.logo = encoded_logo.value();
    const Value& lines = member(root, "lines");
    if (!lines.is_array()) {
        fail(at(lines, "\"lines\" must be an array of strings"));
    }
    for (const Value& line : lines.array()) {
        if (!line.is_string()) {
            fail(at(line, "a line must be a string"));
        }
        const Result<std::string> encoded = encode_roll_markup(line.string(), d.font);
        if (!encoded) {
            fail(at(line, encoded.error()));
        }
        d.lines.push_back(encoded.value());
    }
    return Result<StaffRollData>::ok(std::move(d));
} catch (const std::exception& e) {
    return Result<StaffRollData>::fail(e.what());
}

Result<StaffRollData> load_staff_roll(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                      std::string* source) {
    namespace fs = std::filesystem;
    const fs::path relative = fs::path("menu") / "staff_roll.json";
    // Base + every active mod's patch, in load order.
    std::string where;
    const Result<std::string> read = mods::read_data_text(gamedata_dir, mods_dir, relative, &where);
    if (source != nullptr) {
        *source = where;
    }
    if (!read) {
        return Result<StaffRollData>::fail(read.error());
    }
    const std::string& text = read.value();
    Result<StaffRollData> parsed = parse_staff_roll(text);
    if (!parsed) {
        return Result<StaffRollData>::fail(where + ": " + parsed.error());
    }
    return parsed;
}

}  // namespace sh::screens
