// SPDX-License-Identifier: GPL-3.0-or-later
// The font all game text is drawn with. Its base is the 12x16 cell font: the disc's
// letters from the import (convert/fonts) with the glyph sheets of gamedata/font and of the active mods laid over
// them. A character that font lacks is drawn from another font into the same kind of cells and kept: a bitmap or
// outline font of a mod, a family the language asks for, a font of gamedata/font, then the best-suited system
// font (i18n/core/font_catalog.h).
//
// Text is UTF-8. A byte below 0x80 keeps the meaning the original font gives it (the English text is written with
// those bytes); anything else is a character. Glyphs are addressed the way the drawer addresses textures: the
// disc's cells in the disc's own strip, every other cell in one of the font's pages, which are textures of their
// own registered with the renderer under the texture page (768, 0) and one palette position each.
#pragma once

#include "engine/text/text_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sh::i18n {

class GameFont {
public:
    // The game's one font. Loaded on first use, once the game data is there.
    static GameFont& get();

    // False when the font could not be loaded (no import): nothing is drawn then.
    bool active();
    // Reads everything again: the mods changed.
    void reload();
    // The font families the language in force wants for characters the cell font lacks, best first.
    void set_preferred_families(std::vector<std::string> families);
    // Rows of the cells the language in force draws those characters into (lang.json "glyph_cell_height"): taller
    // than the cell font's 16 for a script whose letters reach far above and below the line, so they are not
    // shrunk to fit; the lines of text are spaced by it too.
    static constexpr int kMaxDrawnCellHeight = 32;
    void set_drawn_cell_height(int rows);

    // One character as 0..2 glyph emissions: a byte below 0x80 as the drawer passes it ('!' and '&' already turned
    // into the bytes of their cells), anything else a code point. A character no font has draws the fallback box.
    int map(std::uint32_t code, sh::text::GlyphEmit out[2]);
    // The cursor step of that character.
    int advance(std::uint32_t code);
    // Whether the character has a glyph of its own, in the cell font or in a font it can be drawn from.
    bool has(char32_t cp);

    // The geometry the drawer needs beside the emissions.
    sh::text::FontAtlas atlas();
    // Uploads the pages that changed since the last call. The text host calls it when a string has been drawn,
    // which is before the frame is rendered.
    void flush();

    // What the font consists of, for the `langcheck` console command.
    void describe(void (*out)(const char*, ...));

private:
    GameFont();
    ~GameFont();
    struct Impl;
    Impl* impl_;
};

}  // namespace sh::i18n
