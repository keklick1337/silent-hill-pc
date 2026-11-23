#include "formats/convert/font/game_font.h"

#include "formats/convert/text/mini_json.h"
#include "formats/convert/tim/tim_file.h"
#include "formats/png/sh_png.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

namespace sh::convert::font {

namespace json = sh::convert::text::json;

namespace {

// The disc font: 84 cells for bytes 39..122. Most draw their own ASCII character; these draw something else, and
// the game's text is written with the byte, so the byte keeps that meaning (i18n/game_font.cpp).
struct DiscOddCell {
    int cell;
    char32_t draws;
};
constexpr DiscOddCell kDiscOdd[] = {
    {'*' - 39, U'='},      {'=' - 39, U'—'}, {'@' - 39, U'★'}, {'[' - 39, U'“'},
    {'\\' - 39, U'!'},     {']' - 39, U'”'}, {'^' - 39, U'&'},      {'`' - 39, U'’'},
};
constexpr int kDiscBlankCell = '_' - 39;  // the byte is the game's space; its cell is empty
constexpr int kDiscCells = 84;

// Advances of the disc cells, as the game's text drawer steps them (FONT_12X16_GLYPH_WIDTHS of the retail font).
// They are what keeps English text where the original put it.
constexpr std::uint8_t kDiscAdvance[kDiscCells] = {
    3,  7,  7,  11, 11, 4,  10, 4,  6,  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 4,  4,
    10, 11, 10, 8,  13, 12, 12, 12, 13, 11, 11, 13, 12, 9,  9,  12, 12, 13, 12, 13, 11,
    13, 12, 10, 11, 13, 12, 12, 12, 11, 12, 6,  4,  6,  8,  0,  3,  9,  10, 9,  9,  9,
    7,  11, 11, 6,  6,  10, 6,  13, 11, 10, 11, 10, 8,  8,  7,  10, 10, 12, 10, 10, 9};

std::string trim(std::string_view s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

std::vector<std::string> words(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        std::size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\t') ++j;
        if (j > i) out.emplace_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

bool parse_int(std::string_view s, int& out) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

bool is_ink(std::uint8_t v) { return v > 1; }  // 0 transparent, 1 the drop shadow

// Ink bounding box of a cell; false when it has no ink.
bool ink_box(const Cell& c, int& x0, int& y0, int& x1, int& y1) {
    x0 = kCellWidth;
    y0 = kCellHeight;
    x1 = -1;
    y1 = -1;
    for (int y = 0; y < kCellHeight; ++y) {
        for (int x = 0; x < kCellWidth; ++x) {
            if (is_ink(c.at(x, y))) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    return x1 >= 0;
}

// Copies `src` shifted by (dx, dy) over `dst`, every non-transparent pixel; pixels leaving the cell are dropped.
void overlay(Cell& dst, const Cell& src, int dx, int dy) {
    for (int y = 0; y < kCellHeight; ++y) {
        for (int x = 0; x < kCellWidth; ++x) {
            const std::uint8_t v = src.at(x, y);
            const int tx = x + dx;
            const int ty = y + dy;
            if (v == 0 || tx < 0 || ty < 0 || tx >= kCellWidth || ty >= kCellHeight) continue;
            // A shadow pixel never covers ink already there.
            if (v == 1 && is_ink(dst.at(tx, ty))) continue;
            dst.at(tx, ty) = v;
        }
    }
}

// The 4-bit TIM's pixels as one index per byte; the picture's width in *width.
Result<std::vector<std::uint8_t>> tim_indices(const tim::TimFile& t, int& width, int& height) {
    if (t.depth != tim::Depth::Bpp4) return Result<std::vector<std::uint8_t>>::fail("not a 4-bit TIM");
    width = static_cast<int>(t.image_words) * 4;
    height = t.image_height;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width * height));
    for (int i = 0; i < width * height; ++i) {
        const std::uint8_t b = t.data[static_cast<std::size_t>(i / 2)];
        out[static_cast<std::size_t>(i)] = (i & 1) ? (b >> 4) : (b & 15);
    }
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

// A capital's top is at row 0..2; a mark over it goes above the cell. A lowercase letter leaves rows 0..3 free.
constexpr int kCapitalTop = 3;
// The top row of a lowercase letter without an ascender.
constexpr int kLowercaseTop = 4;

}  // namespace

std::uint8_t ink_index(int y) {
    static constexpr std::uint8_t kRows[kCellHeight] = {10, 11, 12, 13, 14, 15, 15, 14, 13, 12, 11, 10, 10, 10, 10, 10};
    return kRows[std::clamp(y, 0, kCellHeight - 1)];
}

const Glyph* GameFont::find(char32_t cp) const {
    const auto it = glyphs.find(cp);
    return it == glyphs.end() ? nullptr : &it->second;
}

int GameFont::add_cell(const Cell& cell, int first_shared) {
    for (int i = std::max(first_shared, disc_cells); i < static_cast<int>(cells.size()); ++i) {
        if (cells[static_cast<std::size_t>(i)] == cell) return i;
    }
    if (static_cast<int>(cells.size()) >= kMaxCells) return -1;
    cells.push_back(cell);
    return static_cast<int>(cells.size()) - 1;
}

std::string codepoint_name(char32_t cp) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "U+%04X", static_cast<unsigned>(cp));
    return buf;
}

bool parse_codepoint(std::string_view text, char32_t& out) {
    if (text.size() < 3 || text[0] != 'U' || text[1] != '+') return false;
    unsigned v = 0;
    const auto r = std::from_chars(text.data() + 2, text.data() + text.size(), v, 16);
    if (r.ec != std::errc() || r.ptr != text.data() + text.size() || v > 0x10FFFF) return false;
    out = static_cast<char32_t>(v);
    return true;
}

std::string utf8(char32_t cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

// ---- sheet ---------------------------------------------------------------------------------------------------

Result<GlyphSheet> parse_sheet(std::string_view text, std::string_view name) {
    GlyphSheet sheet;
    std::vector<std::string> lines;
    for (std::size_t i = 0; i <= text.size();) {
        const std::size_t j = std::min(text.find('\n', i), text.size());
        lines.push_back(trim(text.substr(i, j - i)));
        i = j + 1;
    }
    const auto fail = [&](std::size_t line, const std::string& what) {
        return Result<GlyphSheet>::fail(std::string(name) + ":" + std::to_string(line + 1) + ": " + what);
    };
    const auto read_block = [&](std::size_t& i, Cell& cell) -> std::string {
        std::array<bool, kCellWidth * kCellHeight> ink{};
        for (int y = 0; y < kCellHeight; ++y) {
            ++i;
            if (i >= lines.size()) return "the block ends early (16 rows of 12 characters)";
            const std::string& row = lines[i];
            if (row.size() != static_cast<std::size_t>(kCellWidth)) return "row " + std::to_string(y + 1) + " is not 12 characters";
            for (int x = 0; x < kCellWidth; ++x) {
                const char c = row[static_cast<std::size_t>(x)];
                if (c == '.') continue;
                if (c == '#') {
                    ink[static_cast<std::size_t>(y * kCellWidth + x)] = true;
                    cell.at(x, y) = ink_index(y);
                } else if (c >= '1' && c <= '9') {
                    cell.at(x, y) = static_cast<std::uint8_t>(c - '0');
                } else if (c >= 'a' && c <= 'f') {
                    cell.at(x, y) = static_cast<std::uint8_t>(c - 'a' + 10);
                } else {
                    return std::string("unknown pixel '") + c + "'";
                }
            }
        }
        // The drop shadow of '#' ink: one texel right and down, under nothing else.
        for (int y = 0; y + 1 < kCellHeight; ++y) {
            for (int x = 0; x + 1 < kCellWidth; ++x) {
                if (ink[static_cast<std::size_t>(y * kCellWidth + x)] && cell.at(x + 1, y + 1) == 0) cell.at(x + 1, y + 1) = 1;
            }
        }
        return {};
    };

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string> w = words(line);
        if (w[0] == "glyph") {
            SheetGlyph g;
            if (w.size() != 4 || !parse_codepoint(w[1], g.cp) || w[2] != "advance" || !parse_int(w[3], g.advance) ||
                g.advance < 0 || g.advance > 32) {
                return fail(i, "expected 'glyph U+XXXX advance N'");
            }
            const std::size_t at = i;
            if (std::string e = read_block(i, g.cell); !e.empty()) return fail(at, e);
            sheet.glyphs.push_back(g);
        } else if (w[0] == "mark") {
            if (w.size() != 2) return fail(i, "expected 'mark <name>'");
            Cell c;
            const std::size_t at = i;
            if (std::string e = read_block(i, c); !e.empty()) return fail(at, e);
            sheet.marks[w[1]] = c;
        } else if (w[0] == "alias") {
            char32_t a = 0;
            char32_t b = 0;
            if (w.size() != 3 || !parse_codepoint(w[1], a) || !parse_codepoint(w[2], b)) return fail(i, "expected 'alias U+XXXX U+YYYY'");
            sheet.aliases.emplace_back(a, b);
        } else if (w[0] == "compose") {
            SheetCompose c;
            const bool below = w.size() == 5 && w[4] == "below";
            if ((w.size() != 4 && !below) || !parse_codepoint(w[1], c.cp) || !parse_codepoint(w[2], c.base)) {
                return fail(i, "expected 'compose U+XXXX U+BASE <mark> [below]'");
            }
            c.mark = w[3];
            c.below = below;
            sheet.composes.push_back(c);
        } else if (w[0] == "flip") {
            char32_t a = 0;
            char32_t b = 0;
            if (w.size() != 3 || !parse_codepoint(w[1], a) || !parse_codepoint(w[2], b)) return fail(i, "expected 'flip U+XXXX U+BASE'");
            sheet.flips.emplace_back(a, b);
        } else {
            return fail(i, "unknown line '" + w[0] + "'");
        }
    }
    return Result<GlyphSheet>::ok(std::move(sheet));
}

Result<void> apply_sheet(GameFont& font, const GlyphSheet& sheet, std::string_view source) {
    const std::string src(source);
    for (const SheetGlyph& g : sheet.glyphs) {
        const int cell = font.add_cell(g.cell);
        if (cell < 0) return Result<void>::fail(src + ": the atlas is full (" + std::to_string(kMaxCells) + " cells)");
        font.glyphs[g.cp] = Glyph{g.advance, {GlyphPart{cell, 0}}, src};
    }
    for (const auto& [cp, base_cp] : sheet.flips) {
        const Glyph* base = font.find(base_cp);
        int x0, y0, x1, y1;
        if (!base || base->parts.size() != 1 ||
            !ink_box(font.cells[static_cast<std::size_t>(base->parts[0].cell)], x0, y0, x1, y1)) {
            return Result<void>::fail(src + ": flip " + codepoint_name(cp) + ": base " + codepoint_name(base_cp) +
                                      " is not a one-part glyph with ink");
        }
        const Cell base_cell = font.cells[static_cast<std::size_t>(base->parts[0].cell)];
        // Turned half a circle inside its ink box and hung from the lowercase top, the way Spanish sets its opening
        // marks; a tall one moves up just enough to keep a row for the shadow.
        const int top = std::clamp(kCellHeight - 1 - (y1 - y0 + 1), 0, kLowercaseTop);
        Cell turned;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                if (!is_ink(base_cell.at(x, y))) continue;
                const int ty = top + (y1 - y);
                if (ty < kCellHeight) turned.at(x0 + (x1 - x), ty) = ink_index(ty);
            }
        }
        for (int y = kCellHeight - 2; y >= 0; --y) {
            for (int x = kCellWidth - 2; x >= 0; --x) {
                if (is_ink(turned.at(x, y)) && turned.at(x + 1, y + 1) == 0) turned.at(x + 1, y + 1) = 1;
            }
        }
        const int cell = font.add_cell(turned);
        if (cell < 0) return Result<void>::fail(src + ": the atlas is full");
        font.glyphs[cp] = Glyph{base->advance, {GlyphPart{cell, 0}}, src};
    }
    for (const auto& [cp, target] : sheet.aliases) {
        const Glyph* t = font.find(target);
        if (!t) return Result<void>::fail(src + ": alias " + codepoint_name(cp) + " names " + codepoint_name(target) + ", which has no glyph");
        Glyph g = *t;
        g.source = src;
        font.glyphs[cp] = g;
    }
    for (const auto& [name, cell] : sheet.marks) {
        const int at = font.add_cell(cell);
        if (at < 0) return Result<void>::fail(src + ": the atlas is full");
        font.marks[name] = at;
    }
    // Marks of these compositions share cells among themselves only, never a glyph's own cell.
    const int first_mark_cell = static_cast<int>(font.cells.size());
    for (const SheetCompose& c : sheet.composes) {
        const Glyph* base = font.find(c.base);
        const auto mark_it = font.marks.find(c.mark);
        if (!base || base->parts.size() != 1) {
            return Result<void>::fail(src + ": compose " + codepoint_name(c.cp) + ": base " + codepoint_name(c.base) +
                                      " is not a one-part glyph");
        }
        if (mark_it == font.marks.end()) return Result<void>::fail(src + ": compose " + codepoint_name(c.cp) + ": no mark '" + c.mark + "'");
        // Copies: add_cell below may grow the cell vector.
        const Cell base_cell = font.cells[static_cast<std::size_t>(base->parts[0].cell)];
        const Cell mark = font.cells[static_cast<std::size_t>(mark_it->second)];
        int bx0, by0, bx1, by1, mx0, my0, mx1, my1;
        if (!ink_box(base_cell, bx0, by0, bx1, by1) || !ink_box(mark, mx0, my0, mx1, my1)) {
            return Result<void>::fail(src + ": compose " + codepoint_name(c.cp) + ": an empty base or mark");
        }
        // Centred over the letter's ink; one free row above a lowercase letter, none above a capital (the mark then
        // sits above the cell, as a second part).
        const int dx = ((bx0 + bx1) - (mx0 + mx1)) / 2;
        if (c.below) {
            // Hung under the letter, touching its lowest ink row (a cedilla).
            Cell merged = base_cell;
            overlay(merged, mark, dx, by1 + 1 - my0);
            const int cell = font.add_cell(merged);
            if (cell < 0) return Result<void>::fail(src + ": the atlas is full");
            font.glyphs[c.cp] = Glyph{base->advance, {GlyphPart{cell, 0}}, src};
            continue;
        }
        const bool capital = by0 < kCapitalTop;
        const int dy = (capital ? by0 - 1 : by0 - 2) - my1;
        Glyph g;
        g.advance = base->advance;
        g.source = src;
        const GlyphPart base_part = base->parts[0];
        if (dy + my0 >= 0) {
            Cell merged = base_cell;
            overlay(merged, mark, dx, dy);
            const int cell = font.add_cell(merged);
            if (cell < 0) return Result<void>::fail(src + ": the atlas is full");
            g.parts = {GlyphPart{cell, 0}};
        } else {
            Cell shifted;
            overlay(shifted, mark, dx, 0);
            const int cell = font.add_cell(shifted, first_mark_cell);
            if (cell < 0) return Result<void>::fail(src + ": the atlas is full");
            g.parts = {GlyphPart{cell, dy}, base_part};
        }
        font.glyphs[c.cp] = g;
    }
    return Result<void>::ok();
}

// ---- build ---------------------------------------------------------------------------------------------------

Result<GameFont> build(const BuildSources& sources) {
    GameFont font;
    auto disc = tim::parse_tim(sources.font16);
    if (!disc) return Result<GameFont>::fail("FONT16.TIM: " + disc.error());
    const tim::TimFile& t = disc.value();
    int w = 0;
    int h = 0;
    auto idx = tim_indices(t, w, h);
    if (!idx) return Result<GameFont>::fail("FONT16.TIM: " + idx.error());
    if (h != kCellHeight || w < 4 * kAtlasWidth || !t.has_clut || t.clut.words.size() < 16) {
        return Result<GameFont>::fail("FONT16.TIM is " + std::to_string(w) + "x" + std::to_string(h) +
                                      ", not the 84-cell strip of the USA disc");
    }
    std::copy_n(t.clut.words.begin(), 16, font.clut.begin());
    // The strip is four texture pages side by side, 21 cells in each: cell c at x = 256 (c / 21) + 12 (c % 21).
    for (int c = 0; c < kDiscCells; ++c) {
        Cell cl;
        const int ox = (c / kColumns) * kAtlasWidth + (c % kColumns) * kCellWidth;
        for (int y = 0; y < kCellHeight; ++y) {
            for (int x = 0; x < kCellWidth; ++x) cl.at(x, y) = idx.value()[static_cast<std::size_t>(y * w + ox + x)];
        }
        font.cells.push_back(cl);
    }
    font.disc_cells = kDiscCells;
    for (int c = 0; c < kDiscCells; ++c) {
        if (c == kDiscBlankCell) continue;
        char32_t cp = static_cast<char32_t>(c + 39);
        for (const DiscOddCell& o : kDiscOdd) {
            if (o.cell == c) cp = o.draws;
        }
        font.glyphs[cp] = Glyph{kDiscAdvance[c], {GlyphPart{c, 0}}, "disc"};
    }

    for (const auto& [name, text] : sources.sheets) {
        auto sheet = parse_sheet(text, name);
        if (!sheet) return Result<GameFont>::fail(sheet.error());
        std::string label = name;
        if (const std::size_t dot = label.find('.'); dot != std::string::npos) label.resize(dot);
        if (auto r = apply_sheet(font, sheet.value(), label); !r) return Result<GameFont>::fail(r.error());
    }
    return Result<GameFont>::ok(std::move(font));
}

GameFont disc_part(const GameFont& font) {
    GameFont out;
    out.clut = font.clut;
    out.disc_cells = std::min(font.disc_cells, static_cast<int>(font.cells.size()));
    out.cells.assign(font.cells.begin(), font.cells.begin() + out.disc_cells);
    for (const auto& [cp, glyph] : font.glyphs) {
        const bool from_disc = std::all_of(glyph.parts.begin(), glyph.parts.end(),
                                           [&](const GlyphPart& p) { return p.cell < out.disc_cells; });
        if (from_disc && glyph.source == "disc") out.glyphs[cp] = glyph;
    }
    return out;
}

Result<std::vector<std::pair<std::string, std::string>>> read_sheets(const std::filesystem::path& font_dir) {
    using R = Result<std::vector<std::pair<std::string, std::string>>>;
    namespace fs = std::filesystem;
    const auto slurp = [](const fs::path& p, std::string& out) {
        std::ifstream in(p, std::ios::binary);
        if (!in) return false;
        out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return true;
    };
    std::vector<fs::path> files;
    std::error_code ec;
    const fs::path list = font_dir / "font.json";
    if (fs::is_regular_file(list, ec)) {
        std::string text;
        if (!slurp(list, text)) return R::fail(list.string() + ": cannot be read");
        auto doc = json::parse(text);
        if (!doc) return R::fail(list.string() + ": " + doc.error());
        const json::Value* format = doc.value().find("format");
        const json::Value* version = doc.value().find("version");
        const json::Value* sheets = doc.value().find("sheets");
        if (!format || format->string != "sh-font-set" || !version || version->integer != 1 || !sheets ||
            sheets->kind != json::Value::Kind::Array) {
            return R::fail(list.string() + ": not an sh-font-set v1 document with \"sheets\"");
        }
        for (const json::Value& name : sheets->items) {
            const fs::path rel(name.string);
            if (name.kind != json::Value::Kind::String || rel.empty() || rel.is_absolute() ||
                std::find(rel.begin(), rel.end(), fs::path("..")) != rel.end()) {
                return R::fail(list.string() + ": \"sheets\" holds paths inside the font directory");
            }
            files.push_back(font_dir / rel);
        }
    } else {
        for (const auto& e : fs::directory_iterator(font_dir / "glyphs", ec)) {
            if (e.is_regular_file(ec) && e.path().extension() == ".glyphs") files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
    }
    std::vector<std::pair<std::string, std::string>> out;
    for (const fs::path& f : files) {
        std::string text;
        if (!slurp(f, text)) return R::fail(f.string() + ": cannot be read");
        out.emplace_back(f.filename().string(), std::move(text));
    }
    return R::ok(std::move(out));
}

// ---- files ---------------------------------------------------------------------------------------------------

namespace {

std::array<std::uint8_t, 4> clut_rgba(std::uint16_t c) {
    const int r = c & 31;
    const int g = (c >> 5) & 31;
    const int b = (c >> 10) & 31;
    return {static_cast<std::uint8_t>((r << 3) | (r >> 2)), static_cast<std::uint8_t>((g << 3) | (g >> 2)),
            static_cast<std::uint8_t>((b << 3) | (b >> 2)), static_cast<std::uint8_t>(c == 0 ? 0 : 255)};
}

}  // namespace

std::vector<std::uint8_t> atlas_rgba(const GameFont& font) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(kAtlasWidth * kAtlasHeight * 4), 0);
    for (int c = 0; c < static_cast<int>(font.cells.size()); ++c) {
        for (int y = 0; y < kCellHeight; ++y) {
            for (int x = 0; x < kCellWidth; ++x) {
                const std::uint8_t v = font.cells[static_cast<std::size_t>(c)].at(x, y);
                if (v == 0) continue;
                const auto px = clut_rgba(font.clut[v]);
                std::copy(px.begin(), px.end(),
                          out.begin() + static_cast<std::ptrdiff_t>(((cell_v(c) + y) * kAtlasWidth + cell_u(c) + x) * 4));
            }
        }
    }
    return out;
}

Result<std::vector<std::uint8_t>> write_atlas_png(const GameFont& font) {
    png::Image img;
    img.width = kAtlasWidth;
    img.height = kAtlasHeight;
    img.color_type = png::ColorType::Indexed;
    img.pixels.assign(static_cast<std::size_t>(kAtlasWidth * kAtlasHeight), 0);
    for (int c = 0; c < static_cast<int>(font.cells.size()); ++c) {
        for (int y = 0; y < kCellHeight; ++y) {
            for (int x = 0; x < kCellWidth; ++x) {
                img.pixels[static_cast<std::size_t>((cell_v(c) + y) * kAtlasWidth + cell_u(c) + x)] =
                    font.cells[static_cast<std::size_t>(c)].at(x, y);
            }
        }
    }
    for (int i = 0; i < 16; ++i) {
        const auto px = clut_rgba(font.clut[static_cast<std::size_t>(i)]);
        img.palette.insert(img.palette.end(), {px[0], px[1], px[2]});
        img.palette_alpha.push_back(px[3]);
    }
    return png::encode(img);
}

std::string write_metrics_json(const GameFont& font, std::string_view image) {
    std::string s;
    s += "{\n";
    s += "  \"format\": \"sh-font\",\n  \"version\": 1,\n";
    s += "  \"image\": " + json::quote(image) + ",\n";
    s += "  \"cell\": [" + std::to_string(kCellWidth) + ", " + std::to_string(kCellHeight) + "],\n";
    s += "  \"columns\": " + std::to_string(kColumns) + ",\n";
    s += "  \"cells\": " + std::to_string(font.cells.size()) + ",\n";
    s += "  \"disc_cells\": " + std::to_string(font.disc_cells) + ",\n";
    s += "  \"clut\": [";
    for (std::size_t i = 0; i < font.clut.size(); ++i) s += (i ? ", " : "") + std::to_string(font.clut[i]);
    s += "],\n";
    s += "  \"fallback\": \"" + codepoint_name(kFallback) + "\",\n";
    s += "  \"marks\": {";
    bool first_mark = true;
    for (const auto& [name, cell] : font.marks) {
        s += (first_mark ? "" : ", ") + json::quote(name) + ": " + std::to_string(cell);
        first_mark = false;
    }
    s += "},\n";
    s += "  \"glyphs\": [\n";
    bool first = true;
    for (const auto& [cp, g] : font.glyphs) {
        s += first ? "" : ",\n";
        first = false;
        s += "    {\"cp\": \"" + codepoint_name(cp) + "\", \"char\": " + json::quote(utf8(cp)) +
             ", \"advance\": " + std::to_string(g.advance);
        if (g.parts.size() == 1 && g.parts[0].dy == 0) {
            s += ", \"cell\": " + std::to_string(g.parts[0].cell);
        } else {
            s += ", \"parts\": [";
            for (std::size_t i = 0; i < g.parts.size(); ++i) {
                s += (i ? ", " : "") + std::string("[") + std::to_string(g.parts[i].cell) + ", " + std::to_string(g.parts[i].dy) + "]";
            }
            s += "]";
        }
        s += ", \"source\": " + json::quote(g.source) + "}";
    }
    s += "\n  ]\n}\n";
    return s;
}

Result<GameFont> read_font(std::string_view metrics_json, std::span<const std::uint8_t> atlas_png) {
    using R = Result<GameFont>;
    auto doc = json::parse(metrics_json);
    if (!doc) return R::fail("metrics: " + doc.error());
    const json::Value& root = doc.value();
    const json::Value* format = root.find("format");
    const json::Value* version = root.find("version");
    if (!format || format->string != "sh-font" || !version || version->integer != 1) return R::fail("metrics: not sh-font version 1");
    const json::Value* cell = root.find("cell");
    const json::Value* columns = root.find("columns");
    if (!cell || cell->items.size() != 2 || cell->items[0].integer != kCellWidth || cell->items[1].integer != kCellHeight ||
        !columns || columns->integer != kColumns) {
        return R::fail("metrics: only 12x16 cells in 21 columns are supported");
    }
    GameFont font;
    const json::Value* count = root.find("cells");
    const json::Value* disc = root.find("disc_cells");
    const json::Value* clut = root.find("clut");
    if (!count || count->integer < 1 || count->integer > kMaxCells) return R::fail("metrics: bad \"cells\"");
    if (!clut || clut->items.size() != 16) return R::fail("metrics: \"clut\" needs 16 colours");
    for (std::size_t i = 0; i < 16; ++i) font.clut[i] = static_cast<std::uint16_t>(clut->items[i].integer);
    font.disc_cells = disc ? static_cast<int>(std::clamp<std::int64_t>(disc->integer, 0, count->integer)) : 0;

    auto img = png::decode(atlas_png);
    std::vector<std::uint8_t> indices;
    if (img && img.value().color_type == png::ColorType::Indexed && img.value().width == kAtlasWidth &&
        img.value().height == kAtlasHeight) {
        indices = img.value().pixels;
        for (std::uint8_t& v : indices) v = v > 15 ? 0 : v;
    } else {
        // A repainted atlas saved as RGBA: back to the palette by the nearest colour.
        auto rgba = png::decode_rgba8(atlas_png);
        if (!rgba) return R::fail("atlas: " + rgba.error());
        if (rgba.value().width != kAtlasWidth || rgba.value().height != kAtlasHeight) {
            return R::fail("atlas: " + std::to_string(rgba.value().width) + "x" + std::to_string(rgba.value().height) + ", not 256x256");
        }
        indices.assign(static_cast<std::size_t>(kAtlasWidth * kAtlasHeight), 0);
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const std::uint8_t* p = &rgba.value().pixels[i * 4];
            if (p[3] < 128) continue;
            int best = 1;
            int best_d = std::numeric_limits<int>::max();
            for (int k = 1; k < 16; ++k) {
                const auto q = clut_rgba(font.clut[static_cast<std::size_t>(k)]);
                const int d = (p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]);
                if (d < best_d) {
                    best_d = d;
                    best = k;
                }
            }
            indices[i] = static_cast<std::uint8_t>(best);
        }
    }
    for (int c = 0; c < count->integer; ++c) {
        Cell cl;
        for (int y = 0; y < kCellHeight; ++y) {
            for (int x = 0; x < kCellWidth; ++x) cl.at(x, y) = indices[static_cast<std::size_t>((cell_v(c) + y) * kAtlasWidth + cell_u(c) + x)];
        }
        font.cells.push_back(cl);
    }

    if (const json::Value* marks = root.find("marks")) {
        for (std::size_t i = 0; i < marks->keys.size(); ++i) {
            const std::int64_t c = marks->items[i].integer;
            if (c < 0 || c >= count->integer) return R::fail("metrics: mark " + marks->keys[i] + ": cell out of range");
            font.marks[marks->keys[i]] = static_cast<int>(c);
        }
    }
    const json::Value* glyphs = root.find("glyphs");
    if (!glyphs || glyphs->kind != json::Value::Kind::Array) return R::fail("metrics: no \"glyphs\" array");
    for (const json::Value& g : glyphs->items) {
        const json::Value* cp = g.find("cp");
        const json::Value* adv = g.find("advance");
        char32_t code = 0;
        if (!cp || !parse_codepoint(cp->string, code) || !adv || adv->integer < 0 || adv->integer > 32) {
            return R::fail("metrics: a glyph without a valid \"cp\" / \"advance\"");
        }
        Glyph out;
        out.advance = static_cast<int>(adv->integer);
        if (const json::Value* src = g.find("source")) out.source = src->string;
        if (const json::Value* c = g.find("cell")) {
            out.parts.push_back(GlyphPart{static_cast<int>(c->integer), 0});
        } else if (const json::Value* parts = g.find("parts")) {
            for (const json::Value& p : parts->items) {
                if (p.items.size() != 2) return R::fail("metrics: " + cp->string + ": a part is [cell, dy]");
                out.parts.push_back(GlyphPart{static_cast<int>(p.items[0].integer), static_cast<int>(p.items[1].integer)});
            }
        }
        if (out.parts.empty() || out.parts.size() > 2) return R::fail("metrics: " + cp->string + ": needs 1 or 2 parts");
        for (const GlyphPart& p : out.parts) {
            if (p.cell < 0 || p.cell >= count->integer || p.dy < -kCellHeight || p.dy > kCellHeight) {
                return R::fail("metrics: " + cp->string + ": cell or dy out of range");
            }
        }
        font.glyphs[code] = std::move(out);
    }
    return R::ok(std::move(font));
}

}  // namespace sh::convert::font
