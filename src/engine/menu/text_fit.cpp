// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/menu/text_fit.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "engine/text/text_types.h"

namespace sh::menu {

namespace {

constexpr const char* kEllipsis = "...";

bool continuation_byte(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

// ASCII bytes the game font draws as something else; their fullwidth forms (U+FF01..) are the characters themselves.
constexpr std::string_view kRemappedAscii = "*=@[\\]^_`";
constexpr char32_t kFullwidthOffset = 0xFEE0;

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// What stands in for a character no font has: its base letter or an ASCII look-alike; "" when there is none.
std::string_view look_alike(char32_t cp) {
    struct Range {
        char32_t first, last;
        std::string_view text;
    };
    static constexpr Range kRanges[] = {
        {0x2018, 0x2019, "'"}, {0x201C, 0x201D, "'"}, {0x2013, 0x2014, "-"}, {0x2026, 0x2026, "..."},
        {0xC0, 0xC5, "A"},     {0xC7, 0xC7, "C"},     {0xC8, 0xCB, "E"},     {0xCC, 0xCF, "I"},
        {0xD1, 0xD1, "N"},     {0xD2, 0xD6, "O"},     {0xD8, 0xD8, "O"},     {0xD9, 0xDC, "U"},
        {0xDD, 0xDD, "Y"},     {0xDF, 0xDF, "ss"},    {0xE0, 0xE5, "a"},     {0xE7, 0xE7, "c"},
        {0xE8, 0xEB, "e"},     {0xEC, 0xEF, "i"},     {0xF1, 0xF1, "n"},     {0xF2, 0xF6, "o"},
        {0xF8, 0xF8, "o"},     {0xF9, 0xFC, "u"},     {0xFD, 0xFD, "y"},     {0xFF, 0xFF, "y"},
    };
    for (const Range& r : kRanges) {
        if (cp >= r.first && cp <= r.last) {
            return r.text;
        }
    }
    return {};
}

}  // namespace

std::string font_spelling(std::string_view utf8, std::size_t max_bytes, const std::function<bool(char32_t)>& font_has) {
    // The reader stops a character at the terminator, so the bytes are read from a terminated copy.
    const std::string source(utf8);
    std::string out;
    std::size_t at = 0;
    while (at < source.size() && source[at] != '\0') {
        std::uint32_t code = 0;
        const int length = sh::text::read_wide_char(source.c_str() + at, code);
        const char32_t cp = code;
        std::string piece;
        if (cp < 0x80) {
            if (cp == ' ') {
                piece = "_";
            } else if (kRemappedAscii.find(static_cast<char>(cp)) != std::string_view::npos) {
                append_utf8(piece, cp + kFullwidthOffset);
            } else if (cp > ' ' && cp < 0x7F) {
                piece = static_cast<char>(cp);
            }
        } else {
            const std::string_view stand_in = look_alike(cp);
            if (font_has(cp) || stand_in.empty()) {
                piece = source.substr(at, static_cast<std::size_t>(length));
            } else {
                piece = stand_in;
            }
        }
        at += static_cast<std::size_t>(length);
        if (piece.empty() || (piece == "_" && !out.empty() && out.back() == '_')) {
            continue;
        }
        if (out.size() + piece.size() > max_bytes) {
            break;
        }
        out += piece;
    }
    return out;
}

std::string date_spelling(int day, int month, int year, int hour, int minute) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%02d._%02d._%04d._%02d:%02d", day, month, year, hour, minute);
    return buf;
}

FittedText fit_with_ellipsis(std::string_view text, int max_width, const TextMeasure& measure) {
    const std::string whole(text);
    if (whole.size() <= kFitLineBytes && measure(whole.c_str()) <= max_width) {
        return {whole, false};
    }

    // The beginnings are tried a byte at a time, shortest first, up to the first that no longer leaves the room.
    const int room = max_width - measure(kEllipsis);
    const std::size_t longest = std::min(whole.size(), kFitLineBytes);
    std::size_t keep = 0;
    for (std::size_t bytes = 0; bytes <= longest; bytes++) {
        if (measure(whole.substr(0, bytes).c_str()) > room) {
            break;
        }
        keep = bytes;
    }
    while (keep > 0 && keep < whole.size() && continuation_byte(whole[keep])) {
        keep--;
    }
    while (keep > 0 && whole[keep - 1] == '_') {
        keep--;
    }

    std::string out = whole.substr(0, keep);
    out += kEllipsis;
    // The drawer's line limit holds for the cut line too.
    if (out.size() > kFitLineBytes) {
        out.resize(kFitLineBytes);
    }
    return {std::move(out), true};
}

}  // namespace sh::menu
