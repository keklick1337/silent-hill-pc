// SPDX-License-Identifier: GPL-3.0-or-later
// Bidirectional ordering of one line of text (Unicode Standard Annex #9, the part without explicit embeddings):
// the weak and neutral rules, paired brackets, implicit levels, reordering, marks kept after their base and
// mirrored characters. Explicit embedding, override and isolate controls are ignored (dropped from the line), which
// is enough for sentences that mix right-to-left words with Latin words, numbers and punctuation. One rule is
// added to the annex: a plus or minus sign directly after a left-to-right word stays with that word.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sh::i18n {

enum class Direction : std::uint8_t { Ltr, Rtl };

struct BidiLine {
    // Embedding level of every character of the line (even = left to right).
    std::vector<std::uint8_t> levels;
    // The characters in the order they are drawn, left to right, as indices into the line. Boundary-neutral
    // characters (the controls above, zero width joiners) are not listed: they draw nothing.
    std::vector<std::size_t> order;
};

// Orders `line` (no line breaks inside) in a paragraph of direction `base`.
BidiLine bidi_line(std::u32string_view line, Direction base);

// The same as a string: the visual order with the characters of right-to-left runs mirrored.
std::u32string bidi_visual(std::u32string_view line, Direction base);

// Whether any character is right-to-left (Hebrew or Arabic letters, Arabic digits): a line without one needs no
// ordering in a left-to-right paragraph.
bool has_rtl(std::u32string_view text);

// The direction of the first letter of the text, `fallback` when it has none.
Direction first_strong(std::u32string_view text, Direction fallback);

}  // namespace sh::i18n
