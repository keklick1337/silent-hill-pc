// SPDX-License-Identifier: GPL-3.0-or-later
// Contextual shaping of Arabic script: every letter becomes the presentation form (isolated, initial, medial or
// final) its neighbours call for, and lam followed by alef becomes one ligature. The result is still text - the
// forms are Unicode characters (U+FB50..U+FDFF, U+FE70..U+FEFF) that any Arabic font carries - so the drawer needs
// no shaping tables of a font.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sh::i18n {

struct ShapedText {
    std::u32string text;
    // For every character of `text`, the index of the input character it stands for (the lam of a ligature).
    std::vector<std::size_t> source;
};

// Shapes text in its logical (reading) order; characters of other scripts pass through.
ShapedText shape_arabic(std::u32string_view logical);

// Whether the text has anything to shape.
bool has_arabic(std::u32string_view text);

}  // namespace sh::i18n
