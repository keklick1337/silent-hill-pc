// Fonts for characters the game font lacks (src/i18n/core): what a font file says about itself, which face the
// catalogue names for a character, and the cell drawing of a glyph. The font read is one the repository ships
// (gamedata/font/Oswald-Regular.ttf); the catalogue rules are checked on made-up faces.

#include "i18n/core/bitmap_font.h"
#include "i18n/core/font_catalog.h"
#include "i18n/core/font_file.h"
#include "i18n/core/language_set.h"
#include "i18n/core/outline_font.h"

#include "framework/sh_test.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using sh::i18n::FontCatalog;
using sh::i18n::FontFace;
using sh::i18n::FontTier;

const fs::path kOswald = fs::path(SH_PROJECT_ROOT) / "gamedata" / "font" / "Oswald-Regular.ttf";

FontFace face(const char* family, const char* style, int weight, std::vector<sh::i18n::CodeSpan> coverage) {
    FontFace f;
    f.family = family;
    f.style = style;
    f.weight = weight;
    f.coverage = std::move(coverage);
    f.file = std::string(family) + ".ttf";
    return f;
}

const std::vector<sh::i18n::CodeSpan> kArabic = {{0x0600, 0x06FF}, {0xFE70, 0xFEFF}};
const std::vector<sh::i18n::CodeSpan> kLatin = {{0x0020, 0x024F}};

}  // namespace

SH_TEST(i18n_font_file_reports_names_weight_and_coverage) {
    const std::vector<FontFace> faces = sh::i18n::read_font_faces(kOswald);
    SH_CHECK(faces.size() == 1);
    const FontFace& f = faces.front();
    SH_CHECK(f.family == "Oswald");
    SH_CHECK(f.weight == 400 && !f.italic && !f.monospace);
    SH_CHECK(f.has(U'A') && f.has(U'é') && f.has(U'Ж'));
    SH_CHECK(!f.has(U'語') && !f.has(U'ب') && !f.has(0x10FFFF));
    // Not a font: nothing, and no failure.
    SH_CHECK(sh::i18n::read_font_faces(fs::path(SH_PROJECT_ROOT) / "AGENTS.md").empty());
    SH_CHECK(sh::i18n::read_font_faces("/no/such/file.ttf").empty());
    const std::vector<std::uint8_t> junk = {'t', 't', 'c', 'f', 0, 1, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3};
    SH_CHECK(sh::i18n::parse_font_faces(junk, "junk.ttc").empty());
}

SH_TEST(i18n_font_catalog_reads_a_directory) {
    FontCatalog catalog;
    SH_CHECK(catalog.add_directory(kOswald.parent_path(), FontTier::GameData) >= 2);
    const FontFace* f = catalog.choose(U'Ж', {});
    SH_CHECK(f != nullptr);
    SH_CHECK(catalog.choose(0x10FFFF, {}) == nullptr);
    SH_CHECK(catalog.add_directory("/no/such/dir", FontTier::System) == 0);
}

SH_TEST(i18n_font_style_score_prefers_a_sturdy_upright_sans) {
    const int bold_sans = sh::i18n::font_style_score(face("Noto Sans Arabic", "Bold", 700, kArabic));
    const int regular_sans = sh::i18n::font_style_score(face("Noto Sans Arabic", "Regular", 400, kArabic));
    const int light_sans = sh::i18n::font_style_score(face("Noto Sans Arabic", "Light", 300, kArabic));
    const int bold_serif = sh::i18n::font_style_score(face("Noto Serif", "Bold", 700, kLatin));
    FontFace italic = face("Noto Sans", "Bold Italic", 700, kLatin);
    italic.italic = true;
    FontFace mono = face("Noto Sans Mono", "Bold", 700, kLatin);
    mono.monospace = true;
    SH_CHECK(bold_sans > regular_sans && regular_sans > light_sans);
    SH_CHECK(bold_sans > bold_serif);
    SH_CHECK(bold_serif > sh::i18n::font_style_score(italic));
    SH_CHECK(regular_sans > sh::i18n::font_style_score(mono));
    SH_CHECK(regular_sans > sh::i18n::font_style_score(face("Noto Color Emoji", "Regular", 400, kLatin)));
}

SH_TEST(i18n_font_catalog_picks_a_face_that_has_the_character) {
    FontCatalog catalog;
    catalog.add_face(face("Latin Only Sans", "Bold", 700, kLatin), FontTier::System);
    catalog.add_face(face("Noto Naskh Arabic", "Regular", 400, kArabic), FontTier::System);
    catalog.add_face(face("Noto Sans Arabic", "Regular", 400, kArabic), FontTier::System);
    catalog.add_face(face("Noto Sans Arabic", "Bold", 700, kArabic), FontTier::System);
    const FontFace* f = catalog.choose(U'ب', {});
    SH_CHECK(f != nullptr && f->family == "Noto Sans Arabic" && f->style == "Bold");
    SH_CHECK(catalog.choose(U'A', {})->family == "Latin Only Sans");
    SH_CHECK(catalog.choose(U'語', {}) == nullptr);
}

SH_TEST(i18n_font_catalog_order_mod_then_language_then_game_then_system) {
    FontCatalog catalog;
    catalog.add_face(face("System Sans", "Bold", 700, kArabic), FontTier::System);
    catalog.add_face(face("Game Font", "Regular", 400, kArabic), FontTier::GameData);
    catalog.add_face(face("Wanted Family", "Light Italic", 300, kArabic), FontTier::System);
    // No wishes: the game data's font wins over the system's, whatever their styles.
    sh::i18n::FontRank rank = sh::i18n::FontRank::System;
    SH_CHECK(catalog.choose(U'ب', {}, {}, &rank)->family == "Game Font" && rank == sh::i18n::FontRank::GameData);
    // A family the language names wins over the game data's font; names compare without case.
    const std::vector<std::string> wanted = {"No Such Family", "wanted family"};
    SH_CHECK(catalog.choose(U'ب', wanted, {}, &rank)->family == "Wanted Family");
    SH_CHECK(rank == sh::i18n::FontRank::Preferred);
    // A mod's font wins over everything.
    catalog.add_face(face("Mod Font", "Regular", 400, kArabic), FontTier::Mod);
    SH_CHECK(catalog.choose(U'ب', wanted, {}, &rank)->family == "Mod Font" && rank == sh::i18n::FontRank::Mod);
    // ... but only for the characters it has.
    catalog.add_face(face("System Latin", "Bold", 700, kLatin), FontTier::System);
    SH_CHECK(catalog.choose(U'A', wanted, {}, &rank)->family == "System Latin" && rank == sh::i18n::FontRank::System);
}

SH_TEST(i18n_font_catalog_keeps_a_script_in_the_face_already_in_use) {
    FontCatalog catalog;
    catalog.add_face(face("Alpha Sans", "Bold", 700, kArabic), FontTier::System);
    catalog.add_face(face("Beta Sans", "Bold", 700, kArabic), FontTier::System);
    const FontFace* first = catalog.choose(U'ب', {});
    SH_CHECK(first != nullptr && first->family == "Alpha Sans");
    const FontFace* beta = &catalog.entries()[1].face;
    const FontFace* const in_use[] = {beta};
    SH_CHECK(catalog.choose(U'ت', {}, in_use) == beta);
}

SH_TEST(i18n_system_font_directories_follow_the_environment) {
    const char* before = std::getenv("SH_FONT_DIRS");
    const std::string saved = before != nullptr ? before : "";
#ifdef _WIN32
    _putenv_s("SH_FONT_DIRS", "C:\\one;D:\\two");
    const std::vector<fs::path> dirs = sh::i18n::system_font_directories();
    SH_CHECK(dirs.size() == 2 && dirs[0] == fs::path("C:\\one") && dirs[1] == fs::path("D:\\two"));
    _putenv_s("SH_FONT_DIRS", before != nullptr ? saved.c_str() : "");
#else
    setenv("SH_FONT_DIRS", "/one:/two/fonts", 1);
    std::vector<fs::path> dirs = sh::i18n::system_font_directories();
    SH_CHECK(dirs.size() == 2 && dirs[0] == fs::path("/one") && dirs[1] == fs::path("/two/fonts"));
    setenv("SH_FONT_DIRS", "", 1);
    SH_CHECK(sh::i18n::system_font_directories().empty());
    unsetenv("SH_FONT_DIRS");
    dirs = sh::i18n::system_font_directories();
    bool shared = false;
    for (const fs::path& d : dirs) {
        shared = shared || d == fs::path("/usr/share/fonts");
    }
    SH_CHECK(shared);
    if (before != nullptr) {
        setenv("SH_FONT_DIRS", saved.c_str(), 1);
    }
#endif
}

SH_TEST(i18n_outline_font_draws_a_glyph_into_game_cells) {
    const std::vector<FontFace> faces = sh::i18n::read_font_faces(kOswald);
    SH_CHECK(!faces.empty());
    const auto font = sh::i18n::OutlineFont::load(faces.front());
    SH_CHECK(font != nullptr);
    SH_CHECK(font->has(U'A') && !font->has(U'語'));
    SH_CHECK(font->units_per_em() > 0 && font->cap_height() > 0);

    sh::i18n::CellStyle style;
    for (auto& row : style.row_color) {
        row = {200, 200, 200};
    }
    style.shadow_color = {8, 8, 8};
    const sh::i18n::FaceFit fit = sh::i18n::fit_face(*font, sh::i18n::ScriptGroup::General, style);
    SH_CHECK(fit.scale > 0.0f && fit.baseline > 8.0f && fit.baseline <= 16.0f);

    const sh::i18n::CellGlyph g = sh::i18n::render_cell_glyph(*font, U'Ж', fit, style);
    SH_CHECK(g.cells == 1 && g.advance >= 4 && g.advance <= 12 && !g.mark);
    const int width = g.cells * style.cell_width * style.scale;
    const int height = style.cell_height * style.scale;
    SH_CHECK(g.rgba.size() == static_cast<std::size_t>(width * height * 4));
    int ink = 0, shadow = 0, top = height, bottom = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* px = &g.rgba[static_cast<std::size_t>((y * width + x) * 4)];
            if (px[3] == 0) {
                continue;
            }
            SH_CHECK(px[3] == 255);  // cut-out art: opaque or absent
            if (px[0] == 200) {
                ++ink;
                top = top < y ? top : y;
                bottom = bottom > y ? bottom : y;
            } else {
                SH_CHECK(px[0] == 8);
                ++shadow;
            }
        }
    }
    SH_CHECK(ink > 200 && shadow > 20);
    // A capital stands on the baseline and is as tall as the game's capitals, within a raster row or two.
    const int cap = bottom - top + 1;
    SH_CHECK(cap >= (style.cap_height - 1) * style.scale && cap <= (style.cap_height + 1) * style.scale);
    SH_CHECK(bottom >= (style.baseline - 1) * style.scale - 1 && bottom <= style.baseline * style.scale);

    // A space has an advance and no cells; a character the face lacks has neither.
    const sh::i18n::CellGlyph space = sh::i18n::render_cell_glyph(*font, U' ', fit, style);
    SH_CHECK(space.cells == 0 && space.advance > 0);
    const sh::i18n::CellGlyph missing = sh::i18n::render_cell_glyph(*font, U'語', fit, style);
    SH_CHECK(missing.cells == 0 && missing.advance == 0);
    // A combining mark is centred in one cell and moves nothing.
    if (font->has(0x0301)) {
        const sh::i18n::CellGlyph mark = sh::i18n::render_cell_glyph(*font, 0x0301, fit, style);
        SH_CHECK(mark.mark && mark.advance == 0 && mark.cells == 1);
    }
    SH_CHECK(sh::i18n::script_group(U'ب') == sh::i18n::ScriptGroup::Arabic);
    SH_CHECK(sh::i18n::script_group(0xFE91) == sh::i18n::ScriptGroup::Arabic);
    SH_CHECK(sh::i18n::script_group(U'ש') == sh::i18n::ScriptGroup::Hebrew);
    SH_CHECK(sh::i18n::script_group(U'語') == sh::i18n::ScriptGroup::Wide);
    SH_CHECK(sh::i18n::script_group(U'Ж') == sh::i18n::ScriptGroup::General);
}

SH_TEST(i18n_bitmap_font_file_and_cell_glyph) {
    // Two 16x16 glyphs: a full square and a single left column.
    std::map<char32_t, std::vector<std::uint8_t>> glyphs;
    glyphs[U'口'] = std::vector<std::uint8_t>(32, 0xFF);
    std::vector<std::uint8_t> bar(32, 0);
    for (int y = 0; y < 16; ++y) bar[static_cast<std::size_t>(y * 2)] = 0x80;
    glyphs[U'｜'] = bar;
    auto font = sh::i18n::BitmapFont::parse(sh::i18n::BitmapFont::write(16, 16, glyphs));
    SH_CHECK(font);
    SH_CHECK(font.value().width() == 16 && font.value().height() == 16 && font.value().size() == 2);
    SH_CHECK(font.value().has(U'口') && font.value().has(U'｜') && !font.value().has(U'A'));
    SH_CHECK(font.value().pixel(font.value().glyph(U'｜'), 0, 5) && !font.value().pixel(font.value().glyph(U'｜'), 1, 5));
    SH_CHECK(!sh::i18n::BitmapFont::parse({'S', 'H', 'B', 'F'}));
    SH_CHECK(!sh::i18n::BitmapFont::parse(std::vector<std::uint8_t>(64, 0)));

    sh::i18n::CellStyle style;
    for (auto& row : style.row_color) row = {200, 200, 200};
    style.shadow_color = {8, 8, 8};
    // 16 columns folded into the 12 of a cell: the square fills the cell, the bar stays one game pixel wide.
    const sh::i18n::CellGlyph square = sh::i18n::render_bitmap_glyph(font.value(), U'口', style);
    SH_CHECK(square.cells == 1 && square.advance == 12);
    const int width = 12 * style.scale;
    SH_CHECK(square.rgba.size() == static_cast<std::size_t>(width * 16 * style.scale * 4));
    SH_CHECK(square.rgba[3] == 255 && square.rgba[0] == 200);
    const sh::i18n::CellGlyph thin = sh::i18n::render_bitmap_glyph(font.value(), U'｜', style);
    const auto alpha = [&](const sh::i18n::CellGlyph& g, int x, int y) {
        return g.rgba[static_cast<std::size_t>(((y * style.scale) * width + x * style.scale) * 4 + 3)];
    };
    const auto red = [&](const sh::i18n::CellGlyph& g, int x, int y) {
        return g.rgba[static_cast<std::size_t>(((y * style.scale) * width + x * style.scale) * 4)];
    };
    SH_CHECK(alpha(thin, 0, 0) == 255 && red(thin, 0, 0) == 200);
    SH_CHECK(alpha(thin, 1, 0) == 0);
    SH_CHECK(alpha(thin, 1, 1) == 255 && red(thin, 1, 1) == 8);  // the drop shadow
    SH_CHECK(alpha(thin, 2, 1) == 0);
    SH_CHECK(sh::i18n::render_bitmap_glyph(font.value(), U'A', style).cells == 0);

    // The Japanese set of the game data.
    auto shinonome = sh::i18n::BitmapFont::load(fs::path(SH_PROJECT_ROOT) / "gamedata" / "font" / "shinonome16.shbf");
    SH_CHECK(shinonome && shinonome.value().size() > 6000);
    SH_CHECK(shinonome.value().has(U'語') && shinonome.value().has(U'あ') && shinonome.value().has(U'。'));
    SH_CHECK(!shinonome.value().has(U'A') && !shinonome.value().has(U'Ж') && !shinonome.value().has(U'한'));
}

SH_TEST(i18n_language_descriptor) {
    std::vector<std::string> warnings;
    auto lang = sh::i18n::parse_language(
        R"({"format": "sh-text-lang", "version": 1, "code": "ar", "name": "العربية", "english_name": "Arabic",
            "direction": "rtl", "glyph_cell_height": 21, "fonts": ["Noto Sans Arabic", "Tahoma"]})",
        "ar", warnings);
    SH_CHECK(lang && warnings.empty());
    SH_CHECK(lang.value().code == "ar" && lang.value().name == "العربية" && lang.value().english_name == "Arabic");
    SH_CHECK(lang.value().direction == sh::i18n::Direction::Rtl && lang.value().glyph_cell_height == 21);
    SH_CHECK(lang.value().fonts.size() == 2 && lang.value().fonts[1] == "Tahoma");

    // The first form of the file: "name" English, "menu" the language's own name, "font" ignored.
    lang = sh::i18n::parse_language(
        R"({"format": "sh-text-lang", "version": 1, "code": "ru", "name": "Russian", "menu": "Русский", "font": "cyrillic"})",
        "ru", warnings);
    SH_CHECK(lang && warnings.empty());
    SH_CHECK(lang.value().name == "Русский" && lang.value().english_name == "Russian");
    SH_CHECK(lang.value().direction == sh::i18n::Direction::Ltr && lang.value().fonts.empty());
    SH_CHECK(lang.value().glyph_cell_height == 16);

    // The directory names the language; an unknown key is reported.
    lang = sh::i18n::parse_language(R"({"format": "sh-text-lang", "version": 1, "code": "xx", "colour": "red"})", "yy",
                                    warnings);
    SH_CHECK(lang && lang.value().code == "yy" && lang.value().name == "yy");
    SH_CHECK(warnings.size() == 2);
    SH_CHECK(!sh::i18n::parse_language(R"({"format": "sh-text", "version": 1})", "zz", warnings));
    SH_CHECK(!sh::i18n::parse_language("not json", "zz", warnings));
}

SH_TEST(i18n_languages_are_found_in_the_text_directories) {
    const fs::path root = fs::temp_directory_path() / "sh_i18n_languages_test";
    fs::remove_all(root);
    const auto write = [&](const char* rel, const char* text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    write("base/en/lang.json", R"({"format": "sh-text-lang", "version": 1, "name": "English"})");
    write("base/ru/lang.json", R"({"format": "sh-text-lang", "version": 1, "name": "Русский"})");
    write("base/de/lang.json", R"({"format": "sh-text-lang", "version": 1, "name": "Deutsch"})");
    write("base/notalang/ui.json", "{}");
    write("mod/he/lang.json", R"({"format": "sh-text-lang", "version": 1, "name": "עברית", "direction": "rtl"})");
    write("mod/ru/lang.json", R"j({"format": "sh-text-lang", "version": 1, "name": "Русский (мод)"})j");
    write("mod/bad/lang.json", "{");

    std::vector<std::string> warnings;
    const std::vector<fs::path> dirs = {root / "base", root / "mod", root / "missing"};
    const std::vector<sh::i18n::Language> found = sh::i18n::discover_languages(dirs, warnings);
    SH_CHECK(found.size() == 4);
    SH_CHECK(found[0].code == "en" && found[1].code == "de" && found[2].code == "he" && found[3].code == "ru");
    SH_CHECK(found[2].direction == sh::i18n::Direction::Rtl);
    SH_CHECK(found[3].name == "Русский (мод)");
    SH_CHECK(found[3].descriptor == root / "mod" / "ru" / "lang.json");
    SH_CHECK(warnings.size() == 1);
    fs::remove_all(root);
}
