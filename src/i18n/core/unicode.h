// SPDX-License-Identifier: GPL-3.0-or-later
// The Unicode properties the text layout reads: bidirectional classes, marks, mirrored and
// paired characters, the Arabic presentation forms, canonical compositions and where a line of text without
// spaces may break. The tables are generated (unicode_data.inc, tools/scripts/gen_unicode_tables.py).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sh::i18n {

enum class BidiClass : std::uint8_t { L, R, AL, EN, ES, ET, AN, CS, NSM, BN, B, S, WS, ON };

BidiClass bidi_class(char32_t cp);
// A nonspacing or enclosing mark: drawn over the character before it, takes no room of its own.
bool is_mark(char32_t cp);
// The character drawn in its place in right-to-left text ('(' for ')'); cp itself when it has none.
char32_t mirrored(char32_t cp);
// The closing bracket of an opening one, the opening one of a closing one; 0 when cp is neither.
char32_t closing_bracket(char32_t open);
char32_t opening_bracket(char32_t close);
// The single character that is `base` followed by `mark`, 0 when there is none.
char32_t compose(char32_t base, char32_t mark);

// The four shapes of an Arabic letter (0 = the letter has no such form).
struct ArabicLetter {
    char32_t isolated = 0;
    char32_t final_form = 0;
    char32_t initial = 0;
    char32_t medial = 0;
    // Joins to the letter after it as well as to the one before.
    bool dual() const { return initial != 0 || medial != 0; }
};
// Null for a character that is not a joining letter.
const ArabicLetter* arabic_letter(char32_t cp);
// The letter a presentation form draws (U+FE91 -> U+0628), cp itself for anything else.
char32_t arabic_base(char32_t form);
// The lam-alef ligature for lam followed by `alef`: isolated and final shapes; false when `alef` forms none.
bool lam_alef(char32_t alef, char32_t& isolated, char32_t& final_form);

// Text of these scripts is written without spaces: a line may break between any two such characters.
bool breaks_anywhere(char32_t cp);
// Characters a line must not begin with (closing punctuation, small kana, prolonged sound mark) or end with
// (opening brackets), for the scripts above.
bool no_line_start(char32_t cp);
bool no_line_end(char32_t cp);
// Occupies a full square in East Asian text (ideographs, kana, Hangul, fullwidth forms, and the dash U+2015,
// the leader U+2025, the reference mark U+203B and the geometric marks U+25A0..U+25EF used there).
bool is_wide(char32_t cp);

// UTF-8. decode reads one character at `pos` and advances it; a malformed byte is returned as itself (Latin-1),
// one byte consumed, so no input makes it loop or fail.
char32_t decode_utf8(std::string_view text, std::size_t& pos);
void append_utf8(std::string& out, char32_t cp);
std::u32string to_utf32(std::string_view text);
std::string to_utf8(std::u32string_view text);

}  // namespace sh::i18n
