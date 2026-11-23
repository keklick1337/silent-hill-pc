// The 12x16 game font: one glyph atlas and its metrics for every language. The
// importer writes the disc's own part (FONT16's 84 cells); the game lays the glyph sheets of gamedata/font over it
// (accented Latin letters composed from the disc's letters, Cyrillic, the project's own symbols), then the mods'
// sheets, and draws all game-font text with the result.
#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert::font {

inline constexpr int kCellWidth = 12;
inline constexpr int kCellHeight = 16;
inline constexpr int kColumns = 21;              // cells per atlas row (252 of the 256 texels of a texture page)
inline constexpr int kRows = 16;                 // rows of one 256-texel-high texture page
inline constexpr int kMaxCells = kColumns * kRows;
inline constexpr int kAtlasWidth = 256;          // the atlas image: one 4-bit texture page
inline constexpr int kAtlasHeight = 256;
inline constexpr char32_t kFallback = 0xFFFD;    // drawn for a character no layer has

// One 12x16 cell, palette indices 0..15 (0 transparent, 1 the drop shadow, higher = brighter ink).
struct Cell {
    std::array<std::uint8_t, kCellWidth * kCellHeight> px{};
    std::uint8_t at(int x, int y) const { return px[static_cast<std::size_t>(y * kCellWidth + x)]; }
    std::uint8_t& at(int x, int y) { return px[static_cast<std::size_t>(y * kCellWidth + x)]; }
    bool operator==(const Cell&) const = default;
};

// One drawn piece of a glyph: an atlas cell, raised or lowered by dy (a capital's accent sits above the cell).
struct GlyphPart {
    int cell = 0;
    int dy = 0;
    bool operator==(const GlyphPart&) const = default;
};

struct Glyph {
    int advance = 0;               // cursor step after the glyph
    std::vector<GlyphPart> parts;  // 1 or 2; the first is drawn first (the mark of a two-part capital)
    std::string source;            // where the glyph came from: disc, cyrillic, symbols, compose, a mod sheet
};

struct GameFont {
    // The palette the indices are painted for: PSX colours (BGR555 with the STP bit), as in FONT16.TIM.
    std::array<std::uint16_t, 16> clut{};
    std::vector<Cell> cells;
    // The first `disc_cells` cells are the disc's FONT16 cells 0.. unchanged, so the game may draw them from the
    // disc's own texture (the English text then stays pixel-identical to the original).
    int disc_cells = 0;
    std::map<char32_t, Glyph> glyphs;
    // Combining marks by name (the sheets' `mark` blocks), as atlas cells, so a later sheet (a mod's) can compose
    // with them.
    std::map<std::string, int, std::less<>> marks;

    const Glyph* find(char32_t cp) const;
    // Adds a cell (or finds an identical one at or past `first_shared`, so compositions share their mark cells);
    // -1 when the atlas is full.
    int add_cell(const Cell& cell, int first_shared = 0);
};

// ---- the readable glyph sheet (sh-glyphs v1) ------------------------------------------------------------------
//   glyph U+0025 advance 11      then 16 rows of 12 characters: '.' transparent, '#' ink (shaded by row, drop
//                                shadow added), '1'..'9' 'a'..'f' an explicit palette index
//   mark <name>                  then 16 rows: a combining mark drawn for a lowercase letter (top rows)
//   alias U+0435 U+0065          the same glyph as another character
//   compose U+00C0 U+0041 grave  a letter plus a mark, centred over it (inside the cell when it fits, else a
//                                second part above the cell)
//   compose U+00E7 U+0063 cedilla below
//                                the mark hung under the letter's lowest ink row instead
//   flip U+00BF U+003F           another character's ink turned half a circle and hung from the lowercase top
// Blank lines and lines starting with '#' are ignored outside the 16-row blocks.
struct SheetGlyph {
    char32_t cp = 0;
    int advance = 0;
    Cell cell;
};
struct SheetCompose {
    char32_t cp = 0;
    char32_t base = 0;
    std::string mark;
    bool below = false;  // under the letter, not over it
};
struct GlyphSheet {
    std::vector<SheetGlyph> glyphs;
    std::map<std::string, Cell, std::less<>> marks;
    std::vector<std::pair<char32_t, char32_t>> aliases;  // (character, the character it draws as)
    std::vector<SheetCompose> composes;
    std::vector<std::pair<char32_t, char32_t>> flips;    // (character, the character it is turned from)
};

// Parses a sheet; `name` goes into error messages ("symbols.glyphs:12: ...").
Result<GlyphSheet> parse_sheet(std::string_view text, std::string_view name);

// Applies a sheet over a font: glyphs replace a character's glyph (a new cell), flips, aliases and compositions
// follow, in that order. `source` labels the new glyphs. Fails when the atlas runs out of cells or a reference is unknown.
Result<void> apply_sheet(GameFont& font, const GlyphSheet& sheet, std::string_view source);

// The row shading of '#' ink: palette index for cell row y.
std::uint8_t ink_index(int y);

// ---- building -----------------------------------------------------------------------------------------------
struct BuildSources {
    std::span<const std::uint8_t> font16;     // 1ST/FONT16.TIM of the disc (the 84-cell US strip)
    std::vector<std::pair<std::string, std::string>> sheets;  // (name, text), applied in order
};
// The disc's cells and characters with the sheets applied in order. No sheets: the disc's part alone.
Result<GameFont> build(const BuildSources& sources);

// The part of a font that is the disc's: its first `disc_cells` cells and the characters drawn from them.
GameFont disc_part(const GameFont& font);

// The glyph sheets of a font directory as (file name, text), in the order they apply: the "sheets" list of its
// font.json ("sh-font-set" v1, paths inside the directory) or, without that file, glyphs/*.glyphs by name.
Result<std::vector<std::pair<std::string, std::string>>> read_sheets(const std::filesystem::path& font_dir);

// ---- files ----------------------------------------------------------------------------------------------------
// The atlas as an indexed PNG (the palette is the CLUT, index 0 transparent), 256x256: cell c at
// ((c % 21) * 12, (c / 21) * 16).
Result<std::vector<std::uint8_t>> write_atlas_png(const GameFont& font);
// The metrics (sh-font v1 JSON). `image` is the atlas file name next to it.
std::string write_metrics_json(const GameFont& font, std::string_view image);

// Reads the metrics and the atlas back. An indexed atlas keeps its indices; an RGBA one (a mod's repaint) is
// matched to the palette by nearest colour, transparent where alpha < 128. The fallback glyph is not required
// here: the disc's part has none.
Result<GameFont> read_font(std::string_view metrics_json, std::span<const std::uint8_t> atlas_png);

// RGBA8 of the atlas through its palette (alpha 0 for index 0), kAtlasWidth x kAtlasHeight.
std::vector<std::uint8_t> atlas_rgba(const GameFont& font);

// Top-left texel of a cell in the atlas.
inline int cell_u(int cell) { return (cell % kColumns) * kCellWidth; }
inline int cell_v(int cell) { return (cell / kColumns) * kCellHeight; }

// "U+00C0" <-> 0xC0.
std::string codepoint_name(char32_t cp);
bool parse_codepoint(std::string_view text, char32_t& out);
// UTF-8 of one codepoint.
std::string utf8(char32_t cp);

}  // namespace sh::convert::font
