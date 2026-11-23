// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/game_font.h"

#include "engine/log/sh_log.h"
#include "engine/mods/asset_files_c.h"
#include "engine/vfs/vfs_c.h"
#include "formats/convert/font/font_converter.h"
#include "formats/convert/font/game_font.h"
#include "i18n/core/bitmap_font.h"
#include "i18n/core/font_catalog.h"
#include "i18n/core/outline_font.h"
#include "i18n/core/text_prepare.h"
#include "i18n/core/unicode.h"
#include "render/textures/replacements.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::i18n {
namespace {

namespace fnt = sh::convert::font;
namespace fs = std::filesystem;

// The texture page all pages of the font are addressed through, and the palette positions that tell them apart.
// Neither is ever loaded with a picture: the textures are registered with the renderer under these keys.
constexpr int kPageX = 768;
constexpr int kPageY = 0;
constexpr int kPageW = 64;   // 256 four-bit texels
constexpr int kPageH = 256;
constexpr int kPageTpage = (kPageX / 64) | ((kPageY / 256) << 4);
constexpr int kKeyColumns = 4;
constexpr int kKeyFirstX = 816;
constexpr int kKeyStepX = 16;
constexpr int kKeyFirstY = 255;
constexpr int kMaxPages = 16;  // the cell font's page and fifteen of drawn glyphs

// The disc's strip, where the disc's own cells are drawn from: 21 cells a texture page from page 16, row 240.
constexpr int kDiscTpage = 0x10;
constexpr int kDiscV = 240;
constexpr int kDiscClut = 0x7FD3;

// Raster pixels per game pixel of the drawn glyphs' pages.
constexpr int kDrawnScale = 4;

// The full-width forms stand for the ASCII character itself, whatever its byte means to the game's text.
constexpr char32_t kFullwidthFirst = 0xFF01;
constexpr char32_t kFullwidthLast = 0xFF5E;
constexpr char32_t kFullwidthOffset = 0xFEE0;

constexpr int kMaxWarnings = 64;
constexpr int kMaxFaceAttempts = 6;

struct PageKey {
    int clut_x;
    int clut_y;
    int packed() const { return (clut_y << 6) | (clut_x >> 4); }
};

PageKey page_key(int page) { return {kKeyFirstX - (page % kKeyColumns) * kKeyStepX, kKeyFirstY - page / kKeyColumns}; }

std::array<std::uint8_t, 3> rgb_of(std::uint16_t color555) {
    const auto wide = [](int v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); };
    return {wide(color555 & 31), wide((color555 >> 5) & 31), wide((color555 >> 10) & 31)};
}

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

std::vector<std::uint8_t> read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {});
}

bool converted_file(const char* relative, fs::path& out) {
    char buf[1024];
    if (!ShVfs_FindConverted(relative, buf, sizeof buf)) {
        return false;
    }
    out = buf;
    return true;
}

// The active mods' folders of one name, the first loaded mod first.
std::vector<fs::path> mod_dirs(const char* name) {
    std::vector<fs::path> dirs;
    ShModAsset_DirEach(
        name, [](const char* dir, void* ctx) { static_cast<std::vector<fs::path>*>(ctx)->emplace_back(dir); }, &dirs);
    std::reverse(dirs.begin(), dirs.end());
    return dirs;
}

std::vector<fs::path> files_with_extension(const fs::path& dir, const char* extension) {
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == extension) {
            files.push_back(e.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace

struct GameFont::Impl {
    // A glyph drawn from another font, kept in a page.
    struct Drawn {
        int advance = 0;
        int cells = 0;  // 0: the fallback box stands in
        int page = 0;
        int cell = 0;
        bool mark = false;
    };
    struct Page {
        std::vector<std::uint8_t> rgba;
        int used = 0;
        bool dirty = false;
    };
    struct Bitmap {
        BitmapFont font;
        std::string name;
        bool mod = false;
    };

    bool tried = false;
    bool loaded = false;
    fnt::GameFont cells;
    bool disc_backed = false;  // the disc's cells are drawn from the disc's own texture
    bool cell_page_registered = false;
    CellStyle style;

    std::vector<std::string> preferred;
    int drawn_height = fnt::kCellHeight;  // the language's cell height for drawn glyphs
    bool catalog_built = false;
    FontCatalog catalog;
    std::vector<Bitmap> bitmaps;
    std::map<const FontFace*, std::unique_ptr<OutlineFont>> fonts;
    std::map<std::pair<const FontFace*, ScriptGroup>, FaceFit> fits;
    std::vector<const FontFace*> in_use;
    std::vector<const FontFace*> unusable;

    std::vector<Page> pages;  // pages[0] is the cell font's and holds no pixels here
    std::unordered_map<char32_t, Drawn> drawn;
    std::unordered_set<char32_t> warned;
    int blank_page = -1;  // a transparent cell, for marks and spaces
    int blank_cell = 0;
    int last_advance = 0;  // of the last character that was not a mark

    // ---- the cell font ----

    bool load_cells() {
        fs::path json_path;
        fs::path png_path;
        bool mod_font = false;
        fnt::GameFont base;
        bool have = false;
        if (converted_file(fnt::kMetricsPath, json_path) && converted_file(fnt::kAtlasPath, png_path)) {
            auto font = fnt::read_font(read_text(json_path), read_bytes(png_path));
            if (font) {
                char mod_png[1024];
                mod_font = ShVfs_FindModConverted(fnt::kAtlasPath, mod_png, sizeof mod_png) != 0;
                base = std::move(font.value());
                have = true;
            } else {
                SH_WARN("[FONT] %s: %s - reading the disc's font file instead", json_path.string().c_str(),
                        font.error().c_str());
            }
        }
        if (!have) {
            // No convert/fonts in the import: the same build from the disc's file.
            size_t size = 0;
            unsigned char* raw = ShVfs_ReadFile("1ST/FONT16.TIM", &size);
            if (raw == nullptr) {
                SH_ERR("[FONT] no convert/fonts/game.font.json and no 1ST/FONT16.TIM in the game data: no text can "
                       "be drawn. Import the disc image again.");
                return false;
            }
            const std::vector<std::uint8_t> font16(raw, raw + size);
            std::free(raw);
            fnt::BuildSources sources;
            sources.font16 = font16;
            auto font = fnt::build(sources);
            if (!font) {
                SH_ERR("[FONT] the game font could not be built: %s", font.error().c_str());
                return false;
            }
            base = std::move(font.value());
        }

        if (mod_font) {
            // A mod's own convert/fonts is a whole font: taken as it is, and its cells are drawn from the page.
            cells = std::move(base);
            disc_backed = false;
        } else {
            // The import holds the disc's letters; the rest of the font is data of the game.
            cells = fnt::disc_part(base);
            disc_backed = cells.disc_cells > 0;
            const fs::path dir = fs::path(PcPort_GetGameDataPath()) / "font";
            auto sheets = fnt::read_sheets(dir);
            if (!sheets) {
                SH_ERR("[FONT] %s", sheets.error().c_str());
            } else {
                if (sheets.value().empty()) {
                    SH_ERR("[FONT] %s has no glyph sheets: only the disc's letters can be drawn", dir.string().c_str());
                }
                apply_sheets(sheets.value(), "", false);
            }
        }
        for (const char* folder : {"fonts", "font"}) {  // "fonts" is where mods kept their sheets at first
            for (const fs::path& dir : mod_dirs(folder)) {
                const std::string mod = dir.parent_path().filename().string();
                std::vector<std::pair<std::string, std::string>> sheets;
                if (std::string(folder) == "font") {
                    auto listed = fnt::read_sheets(dir);
                    if (!listed) {
                        SH_WARN("[FONT] mod %s: %s", mod.c_str(), listed.error().c_str());
                        continue;
                    }
                    sheets = std::move(listed.value());
                } else {
                    for (const fs::path& f : files_with_extension(dir, ".glyphs")) {
                        sheets.emplace_back(f.filename().string(), read_text(f));
                    }
                }
                apply_sheets(sheets, mod, true);
            }
        }
        if (cells.find(fnt::kFallback) == nullptr) {
            SH_WARN("[FONT] the game font has no glyph for U+FFFD: a character no font has draws nothing");
        }
        read_style();
        SH_LOG("[FONT] game font: %zu glyphs in %zu cells%s", cells.glyphs.size(), cells.cells.size(),
               mod_font ? " (a mod's whole font)" : "");
        return true;
    }

    // Lays sheets over the font. A sheet with an error is skipped whole, with the reason in the log.
    void apply_sheets(const std::vector<std::pair<std::string, std::string>>& sheets, const std::string& mod,
                      bool is_mod) {
        for (const auto& [name, text] : sheets) {
            const std::string label = is_mod ? mod + "/" + name : name;
            auto sheet = fnt::parse_sheet(text, label);
            if (!sheet) {
                SH_WARN("[FONT] glyph sheet %s", sheet.error().c_str());
                continue;
            }
            fnt::GameFont before = cells;
            const std::string source = is_mod ? mod : name.substr(0, name.find('.'));
            if (auto r = fnt::apply_sheet(cells, sheet.value(), source); !r) {
                SH_WARN("[FONT] glyph sheet %s: %s - sheet skipped", label.c_str(), r.error().c_str());
                cells = std::move(before);
                continue;
            }
            if (is_mod) {
                SH_LOG("[FONT] mod glyphs %s: %zu glyphs, %zu compositions", label.c_str(),
                       sheet.value().glyphs.size(), sheet.value().composes.size());
            }
        }
    }

    // The look drawn glyphs copy: the shading by row, the shadow, and where the capitals of the cell font stand.
    void read_style() {
        style = CellStyle{};
        style.cell_width = fnt::kCellWidth;
        style.cell_height = drawn_height;
        style.scale = kDrawnScale;
        read_cell_look();
        // A taller cell shares the game font's baseline: its extra rows go above and below in the proportion of the
        // rows above and below that baseline.
        const int extra = drawn_height - fnt::kCellHeight;
        style.row_offset = (extra * style.baseline + fnt::kCellHeight / 2) / fnt::kCellHeight;
        style.baseline += style.row_offset;
    }

    void read_cell_look() {
        for (int y = 0; y < fnt::kCellHeight; ++y) {
            style.row_color[static_cast<std::size_t>(y)] = rgb_of(cells.clut[fnt::ink_index(y)]);
        }
        style.shadow_color = rgb_of(cells.clut[1]);
        const fnt::Glyph* h = cells.find(U'H');
        if (h == nullptr || h->parts.empty()) {
            return;
        }
        const fnt::Cell& cell = cells.cells[static_cast<std::size_t>(h->parts.back().cell)];
        int top = -1;
        int bottom = -1;
        for (int y = 0; y < fnt::kCellHeight; ++y) {
            for (int x = 0; x < fnt::kCellWidth; ++x) {
                if (cell.at(x, y) > 1) {  // ink, not the shadow
                    top = top < 0 ? y : top;
                    bottom = y;
                }
            }
        }
        if (top >= 0 && bottom > top) {
            style.cap_height = bottom - top + 1;
            style.baseline = bottom + 1;
        }
    }

    const fnt::Glyph* cell_glyph(char32_t cp) const {
        if (cp >= kFullwidthFirst && cp <= kFullwidthLast) {
            if (const fnt::Glyph* g = cells.find(cp)) {
                return g;
            }
            cp -= kFullwidthOffset;
        }
        return cells.find(cp);
    }

    // ---- fonts for what the cell font lacks ----

    void build_catalog() {
        if (catalog_built) {
            return;
        }
        catalog_built = true;
        const auto started = std::chrono::steady_clock::now();
        // The last loaded mod's fonts are asked first.
        std::vector<fs::path> mods = mod_dirs("font");
        std::reverse(mods.begin(), mods.end());
        std::size_t mod_faces = 0;
        for (const fs::path& dir : mods) {
            mod_faces += catalog.add_directory(dir, FontTier::Mod);
            add_bitmaps(dir, true);
        }
        const fs::path game = fs::path(PcPort_GetGameDataPath()) / "font";
        const std::size_t game_faces = catalog.add_directory(game, FontTier::GameData);
        add_bitmaps(game, false);
        std::size_t system_faces = 0;
        for (const fs::path& dir : system_font_directories()) {
            system_faces += catalog.add_directory(dir, FontTier::System);
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        SH_LOG("[FONT] fonts for characters the game font lacks: %zu of mods, %zu of the game data, %zu of the "
               "system, %zu bitmap (%lld ms)",
               mod_faces, game_faces, system_faces, bitmaps.size(), static_cast<long long>(ms.count()));
    }

    void add_bitmaps(const fs::path& dir, bool mod) {
        for (const fs::path& f : files_with_extension(dir, ".shbf")) {
            auto font = BitmapFont::load(f);
            if (!font) {
                SH_WARN("[FONT] %s", font.error().c_str());
                continue;
            }
            bitmaps.push_back({std::move(font.value()), f.filename().string(), mod});
        }
    }

    const Bitmap* bitmap_for(char32_t cp, bool mod) const {
        for (const Bitmap& b : bitmaps) {
            if (b.mod == mod && b.font.has(cp)) {
                return &b;
            }
        }
        return nullptr;
    }

    OutlineFont* outline(const FontFace* face) {
        auto it = fonts.find(face);
        if (it == fonts.end()) {
            it = fonts.emplace(face, OutlineFont::load(*face)).first;
            if (it->second == nullptr) {
                SH_WARN("[FONT] %s could not be read as a font", face->file.string().c_str());
            }
        }
        return it->second.get();
    }

    // Draws `cp` from the best font that has it; cells == 0 when none does.
    CellGlyph draw_from_fonts(char32_t cp) {
        build_catalog();
        if (const Bitmap* b = bitmap_for(cp, true)) {
            return render_bitmap_glyph(b->font, cp, style);
        }
        std::vector<const FontFace*> excluded = unusable;
        for (int attempt = 0; attempt < kMaxFaceAttempts; ++attempt) {
            FontRank rank = FontRank::System;
            const FontFace* face = catalog.choose(cp, preferred, in_use, &rank, excluded);
            // A font of the game data comes before any font that is merely installed.
            if (face == nullptr || rank == FontRank::System || rank == FontRank::GameData) {
                if (const Bitmap* b = bitmap_for(cp, false)) {
                    return render_bitmap_glyph(b->font, cp, style);
                }
            }
            if (face == nullptr) {
                break;
            }
            OutlineFont* font = outline(face);
            if (font == nullptr) {
                unusable.push_back(face);
                excluded.push_back(face);
                continue;
            }
            if (!font->has(cp)) {
                excluded.push_back(face);  // its character map promised more than it holds
                continue;
            }
            const ScriptGroup group = script_group(cp);
            auto fit = fits.find({face, group});
            if (fit == fits.end()) {
                fit = fits.emplace(std::pair{face, group}, fit_face(*font, group, style)).first;
            }
            if (std::find(in_use.begin(), in_use.end(), face) == in_use.end()) {
                in_use.push_back(face);
                SH_LOG("[FONT] U+%04X '%s' and characters like it are drawn with %s %s (%s)", static_cast<unsigned>(cp),
                       fnt::utf8(cp).c_str(), face->family.c_str(), face->style.c_str(),
                       face->file.string().c_str());
            }
            CellGlyph glyph = render_cell_glyph(*font, cp, fit->second, style);
            if (glyph.cells == 0 && glyph.advance > 0) {
                // A blank character of the font (a wide space): it needs a cell to carry its advance.
                glyph.cells = 1;
                glyph.rgba.assign(static_cast<std::size_t>(style.cell_width * style.scale * style.cell_height * style.scale * 4), 0);
            }
            return glyph;
        }
        return {};
    }

    int page_side_w() const { return fnt::kAtlasWidth * kDrawnScale; }
    int page_side_h() const { return fnt::kAtlasHeight * kDrawnScale; }
    int drawn_cells_per_page() const { return fnt::kColumns * (fnt::kAtlasHeight / style.cell_height); }
    int drawn_u(int cell) const { return fnt::cell_u(cell); }
    int drawn_v(int cell) const { return (cell / fnt::kColumns) * style.cell_height; }

    // Room for `count` cells side by side in a page; false when every page is full.
    bool take_cells(int count, int& page, int& cell) {
        if (pages.empty()) {
            pages.emplace_back();  // the cell font's
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (pages.size() > 1) {
                Page& last = pages.back();
                // The cells of one glyph stay in one row.
                if (count > 1 && (last.used % fnt::kColumns) + count > fnt::kColumns) {
                    last.used += fnt::kColumns - (last.used % fnt::kColumns);
                }
                if (last.used + count <= drawn_cells_per_page()) {
                    page = static_cast<int>(pages.size()) - 1;
                    cell = last.used;
                    last.used += count;
                    return true;
                }
            }
            if (static_cast<int>(pages.size()) >= kMaxPages) {
                return false;
            }
            pages.emplace_back();
            pages.back().rgba.assign(static_cast<std::size_t>(page_side_w()) * static_cast<std::size_t>(page_side_h()) * 4, 0);
        }
        return false;
    }

    void forget_drawn() {
        SH_LOG("[FONT] the pages of drawn glyphs are full (%zu characters): starting them over", drawn.size());
        drawn.clear();
        blank_page = -1;
        for (std::size_t i = 1; i < pages.size(); ++i) {
            std::fill(pages[i].rgba.begin(), pages[i].rgba.end(), std::uint8_t{0});
            pages[i].used = 0;
            pages[i].dirty = true;
        }
        pages.resize(std::min<std::size_t>(pages.size(), 2));
    }

    bool store(const CellGlyph& glyph, Drawn& out) {
        int page = 0;
        int cell = 0;
        if (!take_cells(glyph.cells, page, cell)) {
            forget_drawn();
            if (!take_cells(glyph.cells, page, cell)) {
                return false;
            }
        }
        Page& p = pages[static_cast<std::size_t>(page)];
        const int cw = style.cell_width * kDrawnScale;
        const int ch = style.cell_height * kDrawnScale;
        const int x0 = drawn_u(cell) * kDrawnScale;
        const int y0 = drawn_v(cell) * kDrawnScale;
        const std::size_t row = static_cast<std::size_t>(glyph.cells * cw) * 4;
        if (glyph.rgba.size() >= row * static_cast<std::size_t>(ch)) {
            for (int y = 0; y < ch; ++y) {
                std::copy_n(glyph.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * row), row,
                            p.rgba.begin() + static_cast<std::ptrdiff_t>((static_cast<std::size_t>(y0 + y) * static_cast<std::size_t>(page_side_w()) + static_cast<std::size_t>(x0)) * 4));
            }
        }
        p.dirty = true;
        out.advance = glyph.advance;
        out.cells = glyph.cells;
        out.mark = glyph.mark;
        out.page = page;
        out.cell = cell;
        return true;
    }

    void need_blank() {
        if (blank_page >= 0) {
            return;
        }
        CellGlyph blank;
        blank.cells = 1;
        Drawn slot;
        if (store(blank, slot)) {
            blank_page = slot.page;
            blank_cell = slot.cell;
        }
    }

    const Drawn& drawn_glyph(char32_t cp) {
        if (const auto it = drawn.find(cp); it != drawn.end()) {
            return it->second;
        }
        CellGlyph glyph = draw_from_fonts(cp);
        if (glyph.cells == 0) {
            // A presentation form no font has: the letter itself, in its isolated shape.
            const char32_t base = arabic_base(cp);
            if (base != cp) {
                glyph = draw_from_fonts(base);
            }
        }
        Drawn slot;
        if (glyph.cells > 0 && !store(glyph, slot)) {
            slot = Drawn{};
        }
        if (slot.cells == 0 && static_cast<int>(warned.size()) < kMaxWarnings && warned.insert(cp).second) {
            SH_WARN("[FONT] no font has U+%04X '%s': it draws as the fallback box (a mod can ship a font in its "
                    "font/ folder)",
                    static_cast<unsigned>(cp), fnt::utf8(cp).c_str());
        }
        return drawn.emplace(cp, slot).first->second;
    }

    // ---- emissions ----

    void place_cell(sh::text::GlyphEmit& e, int cell) const {
        e.located = true;
        e.cell = cell;
        if (disc_backed && cell < cells.disc_cells) {
            e.page = kDiscTpage + cell / fnt::kColumns;
            e.u = (cell % fnt::kColumns) * fnt::kCellWidth;
            e.v = kDiscV;
            e.clut = kDiscClut;
        } else {
            e.page = kPageTpage;
            e.u = fnt::cell_u(cell);
            e.v = fnt::cell_v(cell);
            e.clut = page_key(0).packed();
        }
    }

    void place_drawn(sh::text::GlyphEmit& e, int page, int cell) const {
        e.located = true;
        e.cell = cell;
        e.page = kPageTpage;
        e.u = drawn_u(cell);
        e.v = drawn_v(cell);
        e.clut = page_key(page).packed();
        if (style.cell_height != fnt::kCellHeight) {
            e.height = style.cell_height;
            e.dy = -style.row_offset;
        }
    }

    int emit_cells(const fnt::Glyph& g, sh::text::GlyphEmit out[2]) const {
        const int n = static_cast<int>(std::min<std::size_t>(g.parts.size(), 2));
        for (int i = 0; i < n; ++i) {
            out[i] = sh::text::GlyphEmit{};
            place_cell(out[i], g.parts[static_cast<std::size_t>(i)].cell);
            out[i].dy = g.parts[static_cast<std::size_t>(i)].dy;
            out[i].advance = i == n - 1 ? g.advance : 0;
        }
        return n;
    }

    int map(std::uint32_t code, sh::text::GlyphEmit out[2]) {
        char32_t cp = static_cast<char32_t>(code);
        if (code < 0x80) {
            if (code <= ' ' || code == '_' || code == 0x7F) {
                return 0;
            }
            cp = drawn_character(static_cast<unsigned char>(code));
        }
        if (const fnt::Glyph* g = cell_glyph(cp)) {
            last_advance = g->advance;
            return emit_cells(*g, out);
        }
        const Drawn d = drawn_glyph(cp);
        if (d.cells == 0) {
            const fnt::Glyph* box = cells.find(fnt::kFallback);
            if (box == nullptr) {
                return 0;
            }
            last_advance = box->advance;
            return emit_cells(*box, out);
        }
        out[0] = sh::text::GlyphEmit{};
        out[1] = sh::text::GlyphEmit{};
        if (d.mark) {
            // Over the middle of the character before it: step back, draw, step forward again.
            need_blank();
            if (blank_page < 0) {
                return 0;
            }
            const int back = last_advance / 2 + style.cell_width / 2;
            place_drawn(out[0], blank_page, blank_cell);
            out[0].advance = -back;
            place_drawn(out[1], d.page, d.cell);
            out[1].advance = back;
            return 2;
        }
        last_advance = d.advance;
        place_drawn(out[0], d.page, d.cell);
        if (d.cells == 1) {
            out[0].advance = d.advance;
            return 1;
        }
        out[0].advance = style.cell_width;
        place_drawn(out[1], d.page, d.cell + 1);
        out[1].advance = d.advance - style.cell_width;
        return 2;
    }

    // ---- the renderer ----

    static bool register_page(const char* label, int page, const std::uint8_t* rgba, int w, int h, bool point_sampled) {
        const PageKey key = page_key(page);
        (void)label;
        // Point sampling for gutterless pixel art: neighbours must not bleed in. Pinned: the page's cells in video
        // memory hold other art, whose rewriting is no reason to drop the page.
        sh::textures::ReplacementStore& store = sh::textures::replacements();
        const sh::textures::PictureKey place{kPageX, kPageY, kPageW, kPageH, key.clut_x, key.clut_y, 4};
        const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
        return store.put(place, {rgba, bytes}, w, h, point_sampled) && store.pin(place, true);
    }

    void flush() {
        if (!loaded) {
            return;
        }
        if (!cell_page_registered) {
            const std::vector<std::uint8_t> rgba = fnt::atlas_rgba(cells);
            cell_page_registered = true;  // one try: a renderer that refuses it will refuse it again
            if (register_page("game font", 0, rgba.data(), fnt::kAtlasWidth, fnt::kAtlasHeight, true)) {
                SH_LOG("[FONT] game font page registered (%zu cells)", cells.cells.size());
            } else {
                SH_WARN("[FONT] the game font page could not be registered - only the disc's letters will show");
            }
        }
        for (std::size_t i = 1; i < pages.size(); ++i) {
            Page& p = pages[i];
            if (!p.dirty) {
                continue;
            }
            p.dirty = false;
            const std::string label = "drawn glyphs " + std::to_string(i);
            if (!register_page(label.c_str(), static_cast<int>(i), p.rgba.data(), page_side_w(), page_side_h(), false)) {
                SH_WARN("[FONT] a page of drawn glyphs could not be registered - some characters will not show");
            }
        }
    }

    void reset() {
        *this = Impl{};
    }
};

GameFont::GameFont() : impl_(new Impl) {}
GameFont::~GameFont() { delete impl_; }

GameFont& GameFont::get() {
    static GameFont font;
    return font;
}

bool GameFont::active() {
    Impl& s = *impl_;
    const char* gamedata = PcPort_GetGameDataPath();
    if (!s.tried && gamedata != nullptr && *gamedata != '\0') {
        s.tried = true;
        s.loaded = s.load_cells();
    }
    return s.loaded;
}

void GameFont::reload() {
    // Nothing drawn refers to the pages built before the reload; each is registered again when a glyph is put into it.
    std::vector<std::string> preferred = std::move(impl_->preferred);
    const int drawn_height = impl_->drawn_height;
    impl_->reset();
    impl_->preferred = std::move(preferred);
    impl_->drawn_height = drawn_height;
    if (active()) {
        flush();
    }
}

void GameFont::set_preferred_families(std::vector<std::string> families) {
    Impl& s = *impl_;
    if (families == s.preferred) {
        return;
    }
    s.preferred = std::move(families);
    // Characters drawn so far came from the other language's choice of fonts.
    if (!s.drawn.empty()) {
        s.forget_drawn();
    }
    s.in_use.clear();
}

void GameFont::set_drawn_cell_height(int rows) {
    rows = std::clamp(rows, fnt::kCellHeight, kMaxDrawnCellHeight);
    if (rows == impl_->drawn_height) {
        return;
    }
    impl_->drawn_height = rows;
    // The look and the fits of the faces depend on the cell, and the pages are laid out by it.
    if (impl_->tried) {
        reload();
    }
}

int GameFont::map(std::uint32_t code, sh::text::GlyphEmit out[2]) { return active() ? impl_->map(code, out) : 0; }

int GameFont::advance(std::uint32_t code) {
    sh::text::GlyphEmit emits[2];
    const int n = map(code, emits);
    int total = 0;
    for (int i = 0; i < n; ++i) {
        total += emits[i].advance;
    }
    return total;
}

bool GameFont::has(char32_t cp) {
    if (!active()) {
        return false;
    }
    return impl_->cell_glyph(cp) != nullptr || impl_->drawn_glyph(cp).cells > 0;
}

sh::text::FontAtlas GameFont::atlas() {
    sh::text::FontAtlas f;
    f.columns = fnt::kColumns;
    f.cell_width = fnt::kCellWidth;
    f.cell_height = fnt::kCellHeight;
    f.line_height = impl_->drawn_height;
    f.v_base = kDiscV;
    f.rows_per_page = 1;
    f.tpage_base = kDiscTpage;
    f.packed_clut = kDiscClut;
    // The font draws '"', '#', '$' and '%' too; the disc's letters start at the apostrophe.
    f.glyph_byte_min = '!';
    return f;
}

void GameFont::flush() { impl_->flush(); }

void GameFont::describe(void (*out)(const char*, ...)) {
    if (!active()) {
        out("font: not loaded (no convert/fonts and no 1ST/FONT16.TIM in the game data)");
        return;
    }
    Impl& s = *impl_;
    out("font: %zu characters in %zu cells of the game font (%d of them the disc's)", s.cells.glyphs.size(),
        s.cells.cells.size(), s.cells.disc_cells);
    long steps = 0;
    long counted = 0;
    for (const auto& [cp, d] : s.drawn) {
        if (d.cells > 0 && !d.mark) {
            steps += d.advance;
            ++counted;
        }
    }
    out("font: %zu characters drawn from other fonts in %zu page(s), average step %.1f px; capitals %d px on row %d",
        s.drawn.size(), s.pages.empty() ? std::size_t{0} : s.pages.size() - 1,
        counted > 0 ? static_cast<double>(steps) / static_cast<double>(counted) : 0.0, s.style.cap_height,
        s.style.baseline);
    if (s.catalog_built) {
        out("font: catalogue of %zu faces, %zu bitmap font(s)", s.catalog.entries().size(), s.bitmaps.size());
    } else {
        out("font: the fonts of the system have not been needed yet");
    }
    for (const FontFace* face : s.in_use) {
        out("font:   in use: %s %s (%s)", face->family.c_str(), face->style.c_str(), face->file.string().c_str());
    }
    std::string wanted;
    for (const std::string& f : s.preferred) {
        wanted += (wanted.empty() ? "" : ", ") + f;
    }
    out("font: families the language asks for: %s", wanted.empty() ? "(none)" : wanted.c_str());
}

}  // namespace sh::i18n
