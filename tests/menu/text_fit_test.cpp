// SPDX-License-Identifier: GPL-3.0-or-later
// Menu text cut to a width and data text spelled for the game font (src/engine/menu/text_fit.h).

#include "engine/menu/text_fit.h"
#include "framework/sh_test.h"

#include <cstring>
#include <string>

using namespace sh::menu;

namespace {

// Six units a byte: the measure sees cut characters the way a byte-wide font would.
int six_a_byte(const char* s) { return static_cast<int>(std::strlen(s)) * 6; }

// Six units a character, whatever its bytes.
int six_a_character(const char* s) {
    int n = 0;
    for (; *s != '\0'; s++) {
        n += (static_cast<unsigned char>(*s) & 0xC0) != 0x80 ? 1 : 0;
    }
    return n * 6;
}

bool nothing(char32_t) { return false; }
bool everything(char32_t) { return true; }

std::string spelled(const char* text) { return font_spelling(text, kFitLineBytes, nothing); }

}  // namespace

SH_TEST(text_fit_leaves_what_fits) {
    const FittedText whole = fit_with_ellipsis("Quick_Save", 60, six_a_byte);
    SH_CHECK(whole.text == "Quick_Save" && !whole.cut);
    const FittedText empty = fit_with_ellipsis("", 0, six_a_byte);
    SH_CHECK(empty.text.empty() && !empty.cut);
}

SH_TEST(text_fit_cuts_with_an_ellipsis) {
    // 59 units: nine characters and the dots do not fit, six and the dots (54) do.
    const FittedText cut = fit_with_ellipsis("Quick_Save", 59, six_a_byte);
    SH_CHECK(cut.cut && cut.text == "Quick...");  // the cut never ends in a space
    SH_CHECK(fit_with_ellipsis("Change_Camera", 60, six_a_byte).text == "Change...");
    SH_CHECK(fit_with_ellipsis("Change_Camera", 66, six_a_byte).text == "Change_C...");
    // No room even for the dots: they are all that is left.
    SH_CHECK(fit_with_ellipsis("Change", 5, six_a_byte).text == "...");
    SH_CHECK(fit_with_ellipsis("__", 6, six_a_byte).text == "...");
}

SH_TEST(text_fit_never_splits_a_character) {
    // Cyrillic, two bytes a letter.
    const std::string word = "\xD0\x9F\xD0\xB0\xD1\x83\xD0\xB7\xD0\xB0";  // five letters, 30 units
    const FittedText cut = fit_with_ellipsis(word, 29, six_a_character);
    SH_CHECK(cut.cut && cut.text == "\xD0\x9F...");
    SH_CHECK(fit_with_ellipsis(word, 30, six_a_character).text == word);
    // Eight letters in 40 units: three and the dots.
    SH_CHECK(fit_with_ellipsis(word + "\xD0\xB1\xD0\xB2\xD0\xB3", 40, six_a_character).text ==
             "\xD0\x9F\xD0\xB0\xD1\x83...");
    // A measure that counts bytes lands inside a letter: the cut backs off to the letter's start.
    SH_CHECK(fit_with_ellipsis(word, 36, six_a_byte).text == "\xD0\x9F...");
}

SH_TEST(text_fit_holds_the_line_limit) {
    const std::string longer(200, 'a');
    const auto zero = [](const char*) { return 0; };
    const FittedText cut = fit_with_ellipsis(longer, 1000, zero);  // fits by width, not by length
    SH_CHECK(cut.cut && cut.text.size() == kFitLineBytes);
    SH_CHECK(cut.text.substr(0, 4) == "aaaa" && cut.text.find('.') == std::string::npos);
    const std::string edge(kFitLineBytes, 'b');
    SH_CHECK(!fit_with_ellipsis(edge, 1000, zero).cut);
}

SH_TEST(font_spelling_of_ascii) {
    SH_CHECK(spelled("Left Shift") == "Left_Shift");
    SH_CHECK(spelled("a   b") == "a_b" && spelled("  a ") == "_a_");  // a run of spaces is one
    SH_CHECK(spelled("F9 / Tab") == "F9_/_Tab");
    SH_CHECK(spelled("tab\tnew\nline\x7F") == "tabnewline");  // control characters go
    // Bytes that mean something else to the game font are written as their fullwidth forms.
    SH_CHECK(spelled("[") == "\xEF\xBC\xBB" && spelled("]") == "\xEF\xBC\xBD" && spelled("_") == "\xEF\xBC\xBF");
    SH_CHECK(spelled("*=@\\^`") == "\xEF\xBC\x8A\xEF\xBC\x9D\xEF\xBC\xA0\xEF\xBC\xBC\xEF\xBC\xBE\xEF\xBD\x80");
    SH_CHECK(spelled("a_b") == "a\xEF\xBC\xBF" "b");  // an underscore of the text is not a space
    SH_CHECK(spelled("save 1") == "save_1" && spelled("") == "");
}

SH_TEST(font_spelling_past_ascii) {
    const char* cafe = "caf\xC3\xA9";
    SH_CHECK(font_spelling(cafe, kFitLineBytes, everything) == cafe);  // the font has it
    SH_CHECK(spelled(cafe) == "cafe");                                // it does not: the base letter
    SH_CHECK(spelled("\xC3\x9F") == "ss" && spelled("\xC3\x80\xC3\x91\xC3\x9C\xC3\xBF") == "ANUy");
    SH_CHECK(spelled("\xE2\x80\x9Cq\xE2\x80\x9D \xE2\x80\x93 \xE2\x80\xA6") == "'q'_-_...");
    // No look-alike: the character stays for the font's fallback (a Cyrillic letter, a ligature, a CJK character).
    SH_CHECK(spelled("\xD0\x96") == "\xD0\x96" && spelled("\xC3\x86") == "\xC3\x86");
    SH_CHECK(spelled("\xE6\x97\xA5\xE6\x9C\xAC") == "\xE6\x97\xA5\xE6\x9C\xAC");
    // A byte that is not UTF-8 is read as Latin-1.
    SH_CHECK(spelled("caf\xE9") == "cafe");
    SH_CHECK(font_spelling("caf\xE9", kFitLineBytes, everything) == "caf\xE9");
}

SH_TEST(font_spelling_stops_at_its_limit) {
    SH_CHECK(font_spelling("abcdef", 4, nothing) == "abcd");
    SH_CHECK(font_spelling("ab\xD0\x96\xD0\x96", 5, nothing) == "ab\xD0\x96");  // whole characters only
    SH_CHECK(font_spelling("abc\xE2\x80\xA6x", 5, nothing) == "abc");            // "..." does not fit: the end
    SH_CHECK(font_spelling("abc", 0, nothing).empty());
}

SH_TEST(date_spelling_of_the_save_list) {
    SH_CHECK(date_spelling(5, 11, 2026, 9, 7) == "05._11._2026._09:07");
    SH_CHECK(date_spelling(31, 12, 1999, 23, 59) == "31._12._1999._23:59");
}
