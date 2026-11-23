// SPDX-License-Identifier: GPL-3.0-or-later
// Makes a string of the game ready for the drawer, which lays glyphs out left to right one after another: marks are
// composed with their letters, Arabic letters take their contextual forms, lines of a message that are too wide
// are broken, and every line is put into visual order (bidi.h). The input and the output are the drawer's own
// bytes: the markup stays where it applies and colours follow their characters.
// A string that needs none of this comes back unchanged, so text in Latin or Cyrillic is drawn from the very
// bytes it was before.
#pragma once

#include "i18n/core/bidi.h"

#include <functional>
#include <string>
#include <string_view>

namespace sh::i18n {

enum class TextDialect {
    String,   // menus, items, interface: '_' space, '\n' line break, bytes 2..7 colours, 0x01 kerning
    Message,  // map messages: '_' space, ~N line break, ~C<d> colour, the other ~ codes; ' ' '\t' '\n' are layout
    Plain,    // UTF-8 as people write it, for the TrueType overlays: ' ' space, '\n' line break, no markup
};

struct PrepareOptions {
    TextDialect dialect = TextDialect::String;
    Direction base = Direction::Ltr;
    // Messages only: a line wider than this many pixels is broken (0 = lines are left as written). Lines after an
    // inset code (~T) start further right and get `inset_wrap_width`.
    int wrap_width = 0;
    int inset_wrap_width = 0;
    // The step of one character, asked the way the drawer asks its font: a byte below 0x80 as the byte ('!' and
    // '&' already turned into the bytes of their cells), anything else as the code point. Needed for wrapping.
    std::function<int(char32_t)> advance;
    int space_advance = 6;
    int wide_space_advance = 10;
    // The colour the drawer is in when the string starts (0..7), when the caller knows it: a message starts in the
    // message colour. -1: unknown - characters written before the string's first colour code then cannot be
    // given their colour back once a reordered line has drawn a coloured span before them.
    int initial_color = -1;
};

struct PreparedText {
    bool changed = false;  // false: draw the input itself, `text` is empty
    std::string text;
    int wrapped = 0;       // line breaks added
};

PreparedText prepare_text(std::string_view text, const PrepareOptions& options);

// The character a byte below 0x80 draws in the game's font, whose English text is written with a few bytes
// standing for other characters ('[' ']' are quotation marks, '=' a dash, '*' the equals sign).
char32_t drawn_character(unsigned char byte);

}  // namespace sh::i18n
