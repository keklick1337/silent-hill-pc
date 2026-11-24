// The arithmetic of the screens before the game (src/engine/app/screen_kit): line breaking in spaced and unspaced
// scripts, the canvas scale on any window, scrolling, button rows and the text field's editing.

#include "engine/app/screen_kit/line_edit.h"
#include "engine/app/screen_kit/text_layout.h"
#include "framework/sh_test.h"

#include "i18n/core/unicode.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

using namespace sh::screen_kit;

// One unit per character, two for a wide (CJK) one.
float columns(std::string_view s) {
    float w = 0;
    for (char32_t cp : sh::i18n::to_utf32(s)) w += sh::i18n::is_wide(cp) ? 2.0f : 1.0f;
    return w;
}

std::vector<std::string> wrap(std::string_view s, float width) { return wrap_lines(s, width, columns); }

}  // namespace

SH_TEST(screen_kit_wraps_between_words) {
    const auto lines = wrap("the game data is imported once", 12);
    SH_CHECK((lines == std::vector<std::string>{"the game", "data is", "imported", "once"}));
    for (const auto& l : lines) SH_CHECK(columns(l) <= 12);
}

SH_TEST(screen_kit_keeps_short_text_on_one_line) {
    SH_CHECK((wrap("Import", 40) == std::vector<std::string>{"Import"}));
    SH_CHECK(wrap("", 40).empty());
}

SH_TEST(screen_kit_breaks_on_forced_line_breaks) {
    SH_CHECK((wrap("one\n\ntwo", 40) == std::vector<std::string>{"one", "", "two"}));
    SH_CHECK((wrap("one\n", 40) == std::vector<std::string>{"one"}));
}

SH_TEST(screen_kit_breaks_cjk_without_spaces) {
    // Eight wide characters, two columns each, in lines of six columns: three characters a line.
    const auto lines = wrap("ゲームデータを読", 6);
    SH_CHECK(lines.size() == 3);
    SH_CHECK(lines[0] == "ゲーム");
    for (const auto& l : lines) SH_CHECK(columns(l) <= 6);
}

SH_TEST(screen_kit_never_starts_a_line_with_a_closing_mark) {
    // The full stop would start the second line: the character before it goes down with it.
    const auto lines = wrap("読み込み。完了", 8);
    SH_CHECK((lines == std::vector<std::string>{"読み込", "み。完了"}));
    for (const auto& l : lines) SH_CHECK(columns(l) <= 8);
}

SH_TEST(screen_kit_breaks_a_long_path_after_its_separators) {
    const auto lines = wrap("into /home/someone/games/silent-hill/gamedata; after", 16);
    SH_CHECK((lines == std::vector<std::string>{"into /home/", "someone/games/", "silent-hill/", "gamedata; after"}));
}

SH_TEST(screen_kit_breaks_a_word_without_separators_anywhere) {
    const auto lines = wrap("abcdefghij", 4);
    SH_CHECK((lines == std::vector<std::string>{"abcd", "efgh", "ij"}));
}

SH_TEST(screen_kit_fits_the_design_into_the_window) {
    const Size designs[] = {{720, 460}, {420, 700}};
    // A window of the design's size at density 1: one pixel a unit.
    View v = fit_view(720, 460, {}, designs, 0.75f, 2.2f);
    SH_CHECK(v.scale == 1.0f && v.width == 720.0f && v.height == 460.0f);
    // Twice as large: twice the scale, the same canvas.
    v = fit_view(1440, 920, {}, designs, 0.75f, 2.2f);
    SH_CHECK(v.scale == 2.0f && v.width == 720.0f);
    // A tall window takes the tall design rather than shrinking the wide one.
    v = fit_view(420, 700, {}, designs, 0.75f, 2.2f);
    SH_CHECK(v.scale == 1.0f && v.width == 420.0f);
    // A 4K window does not blow the text up past the limit.
    v = fit_view(3840, 2160, {}, designs, 0.75f, 2.2f);
    SH_CHECK(v.scale == 2.2f);
}

SH_TEST(screen_kit_keeps_touch_targets_at_the_display_density) {
    // A phone in landscape, 2400x1080 at density 2.625 with a cutout of 120 pixels on the left.
    const Size designs[] = {{720, 460}, {420, 700}};
    const View v = fit_view(2400, 1080, {120, 0, 0, 0}, designs, 2.625f, 2.625f * 2.2f);
    SH_CHECK(v.scale == 2.625f);
    SH_CHECK(v.origin_x == 120.0f && v.origin_y == 0.0f);
    SH_CHECK(std::fabs(v.width * v.scale - 2280.0f) < 0.01f);
    SH_CHECK(std::fabs(v.height * v.scale - 1080.0f) < 0.01f);
}

SH_TEST(screen_kit_clamps_the_scroll) {
    SH_CHECK(clamp_scroll(50, 100, 200) == 0.0f);   // everything fits
    SH_CHECK(clamp_scroll(-10, 300, 200) == 0.0f);  // above the top
    SH_CHECK(clamp_scroll(150, 300, 200) == 100.0f);
    SH_CHECK(clamp_scroll(40, 300, 200) == 40.0f);
}

SH_TEST(screen_kit_lays_out_a_row_of_buttons) {
    const float widths[] = {80, 60};
    auto slots = lay_out_buttons(widths, 0, 400, 10, 100);
    SH_CHECK(slots.size() == 2);
    // Equal buttons, centred: 100 + 10 + 100 in 400.
    SH_CHECK(slots[0].x0 == 95.0f && slots[0].x1 == 195.0f && slots[0].row == 0);
    SH_CHECK(slots[1].x0 == 205.0f && slots[1].x1 == 305.0f && slots[1].row == 0);
    // Too narrow for both: one full-width button a row.
    slots = lay_out_buttons(widths, 0, 150, 10, 100);
    SH_CHECK(slots[0].row == 0 && slots[1].row == 1);
    SH_CHECK(slots[1].x0 == 0.0f && slots[1].x1 == 150.0f);
}

SH_TEST(screen_kit_edits_utf8_by_characters) {
    LineEdit e;
    e.set("C:/игры");
    SH_CHECK(e.caret() == e.text().size());
    e.backspace();
    SH_CHECK(e.text() == "C:/игр");
    e.left();
    e.left();
    e.insert("ы");
    SH_CHECK(e.text() == "C:/иыгр");
    e.home();
    e.erase_forward();
    SH_CHECK(e.text() == ":/иыгр");
    e.end();
    e.right();
    SH_CHECK(e.caret() == e.text().size());
    // A caret placed inside a character goes back to its start.
    e.place(3);
    SH_CHECK(e.caret() == 2);
}

SH_TEST(screen_kit_drops_line_breaks_from_pasted_text) {
    LineEdit e;
    e.insert("/media/disc/Silent Hill.cue\r\n");
    SH_CHECK(e.text() == "/media/disc/Silent Hill.cue");
    e.set("a\tb");
    SH_CHECK(e.text() == "ab");
}
