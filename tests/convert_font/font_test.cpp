// The game font (src/formats/convert/font): the glyph sheet format, the build from a FONT16 strip with the sheets of
// gamedata/font, the files round trip, the disc's part the importer writes and a mod sheet laid over the result.

#include "formats/convert/font/font_converter.h"
#include "formats/convert/font/game_font.h"
#include "formats/convert/tim/tim_file.h"

#include "framework/sh_test.h"

#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace f = sh::convert::font;
namespace tim = sh::convert::tim;

// A stand-in for the disc's 84-cell strip: 1024x16 4-bit, cell c drawn at x = 256 (c / 21) + 12 (c % 21) with a
// pixel pattern that names the cell (index c % 14 + 2 in its bottom-right corner) and a column of ink, from row 4
// for the lowercase letters (their x-height) and from row 0 for the rest.
std::vector<std::uint8_t> fake_font16() {
    tim::TimFile t;
    t.depth = tim::Depth::Bpp4;
    t.has_clut = true;
    t.clut.width = 16;
    t.clut.height = 1;
    t.clut.words = {0, 0x8842, 0x8C84, 0x90C6, 0x9508, 0x994A, 0x9D8C, 0xA1CE,
                    0xC631, 0xCE73, 0xD6B5, 0xDEF7, 0xE739, 0xEF7B, 0xF7BD, 0xFFFF};
    t.image_words = 256;
    t.image_height = 16;
    t.data.assign(256u * 2u * 16u, 0);
    const auto put = [&](int x, int y, int v) {
        std::uint8_t& b = t.data[static_cast<std::size_t>(y * 512 + x / 2)];
        b = (x & 1) ? static_cast<std::uint8_t>((b & 0x0F) | (v << 4)) : static_cast<std::uint8_t>((b & 0xF0) | v);
    };
    for (int c = 0; c < 84; ++c) {
        const int ox = (c / 21) * 256 + (c % 21) * 12;
        put(ox + 11, 15, c % 14 + 2);
        const bool lower = c + 39 >= 'a' && c + 39 <= 'z';
        for (int y = lower ? 4 : 0; y < 12; ++y) put(ox + 3, y, 15);
    }
    return tim::write_tim(t);
}

// The game's font as the game makes it: the disc's strip with the sheets of gamedata/font.
f::GameFont built() {
    const std::vector<std::uint8_t> raw = fake_font16();
    auto sheets = f::read_sheets(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "font");
    SH_CHECK(sheets);
    f::BuildSources sources;
    sources.font16 = raw;
    if (sheets) sources.sheets = sheets.value();
    auto font = f::build(sources);
    SH_CHECK(font);
    return font ? font.value() : f::GameFont{};
}

}  // namespace

SH_TEST(font_sheet_shading_and_shadow) {
    std::string text = "# sh-glyphs v1\nglyph U+0025 advance 7\n";
    for (int y = 0; y < 16; ++y) text += (y == 0 || y == 6) ? "#...........\n" : "............\n";
    auto sheet = f::parse_sheet(text, "t.glyphs");
    SH_CHECK(sheet);
    const f::Cell& c = sheet.value().glyphs.at(0).cell;
    SH_CHECK(sheet.value().glyphs[0].cp == U'%' && sheet.value().glyphs[0].advance == 7);
    SH_CHECK(c.at(0, 0) == f::ink_index(0) && c.at(0, 6) == f::ink_index(6));
    SH_CHECK(c.at(1, 1) == 1 && c.at(1, 7) == 1);  // the drop shadow, one texel right and down
    SH_CHECK(c.at(1, 0) == 0);
}

SH_TEST(font_sheet_errors_name_the_line) {
    auto short_row = f::parse_sheet("glyph U+0041 advance 5\n....\n", "bad.glyphs");
    SH_CHECK(!short_row && short_row.error().find("bad.glyphs:1") != std::string::npos);
    auto unknown = f::parse_sheet("\nfrobnicate\n", "bad.glyphs");
    SH_CHECK(!unknown && unknown.error().find("bad.glyphs:2") != std::string::npos);
    SH_CHECK(!f::parse_sheet("alias U+0041\n", "x"));
}

SH_TEST(font_build_covers_the_languages) {
    const f::GameFont font = built();
    SH_CHECK(font.disc_cells == 84);
    SH_CHECK(static_cast<int>(font.cells.size()) <= f::kMaxCells);
    // The disc cells keep the original meaning of their bytes.
    SH_CHECK(font.find(U'A') && font.find(U'A')->parts[0].cell == 'A' - 39 && font.find(U'A')->advance == 12);
    SH_CHECK(font.find(U'=')->parts[0].cell == '*' - 39);
    SH_CHECK(font.find(U'—')->parts[0].cell == '=' - 39);
    SH_CHECK(font.find(U'!')->parts[0].cell == '\\' - 39);
    SH_CHECK(font.find(U'_') && font.find(U'_')->parts[0].cell >= font.disc_cells);  // own art: the byte is the space
    // Own art, the PAL accents, Cyrillic, compositions.
    for (char32_t cp : {U'%', U'"', U'#', U'«', U'»', U'№', U'…', U'€', U'°', U'±', U'→', U'ß', U'é', U'ñ', U'Ж',
                        U'я', U'Ё', U'ё', U'ą', U'ł', U'Ł', U'ż', U'Ź', U'ã', U'Õ', U'Ç', U'œ', U'Œ', U'Ÿ', U'¿', U'¡',
                        f::kFallback}) {
        SH_CHECK(font.find(cp) != nullptr);
    }
    SH_CHECK(font.find(U'%')->parts[0].cell >= font.disc_cells);
    // A capital's accent is a second part above the cell; the letter itself stays the disc cell.
    const f::Glyph* oa = font.find(U'Ó');
    SH_CHECK(oa->parts.size() == 2 && oa->parts[0].dy < 0 && oa->parts[1].cell == 'O' - 39 && oa->advance == 13);
    // A lowercase one is merged into one cell.
    SH_CHECK(font.find(U'ć')->parts.size() == 1 && font.find(U'ć')->parts[0].cell >= font.disc_cells);
    // Aliases share the picture: Cyrillic U+0435 is the Latin e, U+0451 the PAL e with diaeresis.
    SH_CHECK(font.find(U'е')->parts == font.find(U'e')->parts);
    SH_CHECK(font.find(U'ё')->parts == font.find(U'ë')->parts);
    SH_CHECK(font.marks.count("acute") == 1 && font.marks.count("caron") == 1 && font.marks.count("breve") == 1);
    // Turkish: breve and cedilla letters, the dotted capital I next to the dotless i.
    for (char32_t cp : {U'ğ', U'Ğ', U'ş', U'Ş', U'İ', U'ı'}) {
        SH_CHECK(font.find(cp) != nullptr);
    }
    SH_CHECK(font.find(U'İ')->parts.size() == 2 && font.find(U'İ')->parts[1].cell == 'I' - 39);
}

SH_TEST(font_files_round_trip) {
    const f::GameFont font = built();
    auto png = f::write_atlas_png(font);
    SH_CHECK(png);
    const std::string json = f::write_metrics_json(font, "game.png");
    auto back = f::read_font(json, png.value());
    SH_CHECK(back);
    const f::GameFont& b = back.value();
    SH_CHECK(b.cells == font.cells && b.clut == font.clut && b.disc_cells == font.disc_cells);
    SH_CHECK(b.glyphs.size() == font.glyphs.size() && b.marks == font.marks);
    for (const auto& [cp, g] : font.glyphs) {
        const f::Glyph* o = b.find(cp);
        SH_CHECK(o && o->parts == g.parts && o->advance == g.advance && o->source == g.source);
    }
    SH_CHECK(!f::read_font("{\"format\": \"sh-font\", \"version\": 2}", png.value()));
}

SH_TEST(font_latin1_comes_from_the_disc_letters_and_the_sheets) {
    const f::GameFont font = built();
    // Every Latin-1 letter and sign of German, French, Spanish and Italian, none of them a cell of another disc.
    for (const char32_t cp : U"ßàáâäçèéêëìíîïñòóôö×ùúûü¿¡Çœ") {
        if (cp == 0) break;
        const f::Glyph* g = font.find(cp);
        SH_CHECK(g != nullptr);
        if (g) SH_CHECK(g->source == "compose" || g->source == "symbols");
    }
    for (const auto& [cp, g] : font.glyphs) SH_CHECK(g.source != "pal");
    SH_CHECK(font.find(U'é')->advance == font.find(U'e')->advance);
    SH_CHECK(font.find(U'í')->advance == font.find(U'i')->advance);
}

SH_TEST(font_sheet_flip_and_mark_below) {
    f::GameFont font = built();
    // A letter with ink in rows 2..5 of column 2 and one more pixel at its top right.
    std::string text = "glyph U+0058 advance 6\n";
    for (int y = 0; y < 16; ++y) text += y == 2 ? "..#.#.......\n" : (y > 2 && y < 6) ? "..#.........\n" : "............\n";
    text += "mark hook\n";
    for (int y = 0; y < 16; ++y) text += y == 0 ? "...#........\n" : "............\n";
    text += "flip U+0059 U+0058\ncompose U+005A U+0058 hook below\n";
    auto sheet = f::parse_sheet(text, "t.glyphs");
    SH_CHECK(sheet);
    SH_CHECK(f::apply_sheet(font, sheet.value(), "t"));
    // Turned half a circle inside its ink box (columns 2..4, four rows) and hung from row 4: the stem is on the
    // right now and the extra pixel at the bottom left.
    const f::Glyph* y = font.find(U'Y');
    SH_CHECK(y && y->advance == 6 && y->parts.size() == 1);
    const f::Cell& turned = font.cells[static_cast<std::size_t>(y->parts[0].cell)];
    for (int row = 4; row < 8; ++row) SH_CHECK(turned.at(4, row) == f::ink_index(row));
    SH_CHECK(turned.at(2, 7) == f::ink_index(7) && turned.at(2, 4) == 0 && turned.at(3, 3) == 0);
    SH_CHECK(turned.at(5, 8) == 1);  // the drop shadow
    // The mark hangs under the lowest ink row, centred on the ink (columns 2..4 -> column 3).
    const f::Glyph* z = font.find(U'Z');
    SH_CHECK(z && z->parts.size() == 1);
    const f::Cell& hooked = font.cells[static_cast<std::size_t>(z->parts[0].cell)];
    SH_CHECK(hooked.at(3, 6) != 0 && hooked.at(2, 5) == f::ink_index(5));
    SH_CHECK(!f::parse_sheet("flip U+0059\n", "m"));
    SH_CHECK(!f::parse_sheet("compose U+005A U+0058 hook above\n", "m"));
    auto bad = f::parse_sheet("flip U+0059 U+E000\n", "m");
    SH_CHECK(bad && !f::apply_sheet(font, bad.value(), "m"));
}

SH_TEST(font_mod_sheet_overrides_and_composes) {
    f::GameFont font = built();
    std::string text = "glyph U+0041 advance 9\n";
    for (int y = 0; y < 16; ++y) text += y < 12 ? "..##........\n" : "............\n";
    text += "compose U+01CD U+0041 caron\nalias U+0391 U+0041\n";
    auto sheet = f::parse_sheet(text, "mod.glyphs");
    SH_CHECK(sheet);
    SH_CHECK(f::apply_sheet(font, sheet.value(), "mymod"));
    const f::Glyph* a = font.find(U'A');
    SH_CHECK(a->advance == 9 && a->parts[0].cell >= font.disc_cells && a->source == "mymod");
    const f::Glyph* ac = font.find(U'Ǎ');  // the mod's new A with the base font's caron
    SH_CHECK(ac && ac->parts.size() == 2 && ac->parts[1].cell == a->parts[0].cell);
    SH_CHECK(font.find(U'Α')->parts == a->parts);
    auto bad = f::parse_sheet("compose U+01CE U+0061 nosuchmark\n", "m");
    SH_CHECK(bad && !f::apply_sheet(font, bad.value(), "m"));
}

SH_TEST(font_converter_outputs) {
    auto conv = f::make_converter();
    const std::vector<std::uint8_t> raw = fake_font16();
    SH_CHECK(conv->accepts(raw, "1ST/FONT16.TIM") && !conv->accepts(raw, "1ST/FONT8NOC.TIM"));
    auto out = conv->decode(raw, "1ST/FONT16.TIM");
    SH_CHECK(out && out.value().size() == 2);
    SH_CHECK(out.value()[0].relative_path == "fonts/game.png" && out.value()[1].relative_path == "fonts/game.font.json");
    // Without glyph sheets the import holds the disc's part: its 84 cells and the characters they draw.
    const auto& json = out.value()[1].bytes;
    auto font = f::read_font(std::string(json.begin(), json.end()), out.value()[0].bytes);
    SH_CHECK(font && font.value().disc_cells == 84 && font.value().cells.size() == 84);
    SH_CHECK(font.value().find(U'A') && !font.value().find(U'Ж') && !font.value().find(f::kFallback));

    // With the sheets of the game data it is the whole font, which the text tools measure with.
    auto sheets = f::read_sheets(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "font");
    SH_CHECK(sheets);
    auto whole = f::make_converter(sheets.value())->decode(raw, "1ST/FONT16.TIM");
    SH_CHECK(whole && whole.value().size() == 2);
    const auto& whole_json = whole.value()[1].bytes;
    auto full = f::read_font(std::string(whole_json.begin(), whole_json.end()), whole.value()[0].bytes);
    SH_CHECK(full && full.value().find(U'Ж') && full.value().find(U'é') && full.value().find(f::kFallback));
    const f::GameFont same = built();
    SH_CHECK(full.value().cells == same.cells && full.value().glyphs.size() == same.glyphs.size());
}

SH_TEST(font_sheets_of_gamedata_and_the_disc_part) {
    const std::filesystem::path dir = std::filesystem::path(SH_PROJECT_ROOT) / "gamedata" / "font";
    auto sheets = f::read_sheets(dir);
    SH_CHECK(sheets && sheets.value().size() == 4);
    // The order of font.json: the marks of symbols.glyphs come before the compositions that use them.
    SH_CHECK(sheets.value().front().first == "symbols.glyphs" && sheets.value().back().first == "compose.glyphs");
    // A directory without font.json and without glyphs/ has no sheets, and that is not an error.
    auto none = f::read_sheets(dir / "glyphs");
    SH_CHECK(none && none.value().empty());

    // The disc's part of a whole font is what the importer writes, so an import made before the sheets moved to
    // gamedata/font gives the same font once the sheets are laid over it again.
    const f::GameFont whole = built();
    const f::GameFont disc = f::disc_part(whole);
    SH_CHECK(disc.disc_cells == 84 && disc.cells.size() == 84 && disc.marks.empty());
    SH_CHECK(disc.find(U'A') && disc.find(U'=') && !disc.find(U'%') && !disc.find(U'é') && !disc.find(U'Ж'));
    f::GameFont again = disc;
    for (const auto& [name, text] : sheets.value()) {
        auto sheet = f::parse_sheet(text, name);
        SH_CHECK(sheet);
        std::string label = name.substr(0, name.find('.'));
        SH_CHECK(f::apply_sheet(again, sheet.value(), label));
    }
    SH_CHECK(again.cells == whole.cells && again.glyphs.size() == whole.glyphs.size() && again.marks == whole.marks);
    for (const auto& [cp, g] : whole.glyphs) {
        const f::Glyph* o = again.find(cp);
        SH_CHECK(o && o->parts == g.parts && o->advance == g.advance && o->source == g.source);
    }
}
