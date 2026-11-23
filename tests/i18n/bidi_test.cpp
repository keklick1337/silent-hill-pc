// Bidirectional ordering of a line (src/i18n/core/bidi.h) and Arabic shaping (arabic.h). Capital Latin letters stand
// for nothing here: the tests use real Hebrew and Arabic letters and compare the order the characters are drawn in.

#include "i18n/core/arabic.h"
#include "i18n/core/bidi.h"
#include "i18n/core/unicode.h"

#include "framework/sh_test.h"

#include <string>

namespace {

using sh::i18n::Direction;

std::string visual(const char* text, Direction base) {
    return sh::i18n::to_utf8(sh::i18n::bidi_visual(sh::i18n::to_utf32(text), base));
}

std::string shaped(const char* text) {
    return sh::i18n::to_utf8(sh::i18n::shape_arabic(sh::i18n::to_utf32(text)).text);
}

}  // namespace

SH_TEST(i18n_unicode_properties) {
    using sh::i18n::BidiClass;
    SH_CHECK(sh::i18n::bidi_class(U'a') == BidiClass::L);
    SH_CHECK(sh::i18n::bidi_class(U'א') == BidiClass::R);
    SH_CHECK(sh::i18n::bidi_class(U'ب') == BidiClass::AL);
    SH_CHECK(sh::i18n::bidi_class(U'7') == BidiClass::EN);
    SH_CHECK(sh::i18n::bidi_class(U'٣') == BidiClass::AN);
    SH_CHECK(sh::i18n::bidi_class(U' ') == BidiClass::WS);
    SH_CHECK(sh::i18n::bidi_class(U'!') == BidiClass::ON);
    SH_CHECK(sh::i18n::bidi_class(0x0301) == BidiClass::NSM);
    SH_CHECK(sh::i18n::is_mark(0x0301) && sh::i18n::is_mark(0x064E) && !sh::i18n::is_mark(U'a'));
    SH_CHECK(sh::i18n::mirrored(U'(') == U')' && sh::i18n::mirrored(U'»') == U'«' && sh::i18n::mirrored(U'a') == U'a');
    SH_CHECK(sh::i18n::closing_bracket(U'[') == U']' && sh::i18n::opening_bracket(U'」') == U'「');
    SH_CHECK(sh::i18n::compose(U'e', 0x0301) == U'é' && sh::i18n::compose(U'и', 0x0306) == U'й');
    SH_CHECK(sh::i18n::compose(U'x', 0x0301) == 0);
    SH_CHECK(sh::i18n::breaks_anywhere(U'語') && sh::i18n::breaks_anywhere(U'あ') && !sh::i18n::breaks_anywhere(U'a'));
    SH_CHECK(sh::i18n::no_line_start(U'。') && sh::i18n::no_line_end(U'「'));
    SH_CHECK(sh::i18n::is_wide(U'語') && sh::i18n::is_wide(U'한') && sh::i18n::is_wide(U'―') && sh::i18n::is_wide(U'※'));
    SH_CHECK(!sh::i18n::is_wide(U'a') && !sh::i18n::is_wide(U'—') && !sh::i18n::is_wide(U'é'));
}

SH_TEST(i18n_utf8_round_trip_and_stray_bytes) {
    const std::string text = "aé語\xF0\x9F\x98\x80";
    SH_CHECK(sh::i18n::to_utf8(sh::i18n::to_utf32(text)) == text);
    // A byte that is not UTF-8 is its Latin-1 character, as the drawer reads it.
    const std::u32string stray = sh::i18n::to_utf32("\xE9x");
    SH_CHECK(stray.size() == 2 && stray[0] == 0xE9 && stray[1] == U'x');
}

SH_TEST(i18n_bidi_left_to_right_text_keeps_its_order) {
    SH_CHECK(visual("Hello, world (1999).", Direction::Ltr) == "Hello, world (1999).");
    SH_CHECK(!sh::i18n::has_rtl(U"Hello 123"));
    SH_CHECK(sh::i18n::has_rtl(U"a ב"));
}

SH_TEST(i18n_bidi_hebrew_word_in_a_left_to_right_line) {
    // The word is drawn right to left where it stands.
    SH_CHECK(visual("abc שלום def", Direction::Ltr) == "abc םולש def");
    // Two words and the space between them form one run.
    SH_CHECK(visual("abc אב גד def", Direction::Ltr) == "abc דג בא def");
}

SH_TEST(i18n_bidi_right_to_left_paragraph) {
    // The line reads from the right: the first word ends up rightmost, the final period leftmost.
    SH_CHECK(visual("שלום עולם.", Direction::Rtl) == ".םלוע םולש");
    // A Latin word keeps its own order inside it.
    SH_CHECK(visual("אב PlayStation גד", Direction::Rtl) == "דג PlayStation בא");
}

SH_TEST(i18n_bidi_numbers_read_left_to_right) {
    SH_CHECK(visual("אב 1999 גד", Direction::Rtl) == "דג 1999 בא");
    // The separator inside a number belongs to it; a price keeps its sign on the number's side.
    SH_CHECK(visual("אב 12:30 גד", Direction::Rtl) == "דג 12:30 בא");
    SH_CHECK(visual("מחיר 1,250.50", Direction::Rtl) == "1,250.50 ריחמ");
    // Arabic digits after Arabic letters are still one left-to-right number.
    SH_CHECK(visual("رقم ١٢٣", Direction::Rtl) == "١٢٣ مقر");
    SH_CHECK(visual("رقم 123", Direction::Rtl) == "123 مقر");
}

SH_TEST(i18n_bidi_latin_terms_keep_their_signs) {
    // Names of settings inside right-to-left text: the hyphen and the plus stay where they were written.
    SH_CHECK(visual("Hor+", Direction::Rtl) == "Hor+");
    SH_CHECK(visual("אב V-Sync גד", Direction::Rtl) == "דג V-Sync בא");
    SH_CHECK(visual("אב Hor+ גד", Direction::Rtl) == "דג Hor+ בא");
    SH_CHECK(visual("אב C++ גד", Direction::Rtl) == "דג C++ בא");
    SH_CHECK(visual("Ctrl+F9", Direction::Rtl) == "Ctrl+F9");
    // A sign that follows a right-to-left word is still the paragraph's.
    SH_CHECK(visual("אב+", Direction::Rtl) == "+בא");
    // Punctuation of the sentence after a Latin word goes to the paragraph's side.
    SH_CHECK(visual("אב PTV.", Direction::Rtl) == ".PTV בא");
    SH_CHECK(visual("......אב", Direction::Rtl) == "בא......");
}

SH_TEST(i18n_bidi_brackets_are_mirrored_and_stay_around_their_text) {
    // Read from the right: the bracket that opens the word is drawn as ')' on its right side.
    SH_CHECK(visual("(אב)", Direction::Rtl) == "(בא)");
    SH_CHECK(visual("אב (גד) הו", Direction::Rtl) == "וה (דג) בא");
    // Brackets around a Latin word in a right-to-left line stay on both sides of it.
    SH_CHECK(visual("אב (abc) גד", Direction::Rtl) == "דג (abc) בא");
    // A right-to-left word in brackets inside a left-to-right line.
    SH_CHECK(visual("see (שלום) now", Direction::Ltr) == "see (םולש) now");
}

SH_TEST(i18n_bidi_trailing_spaces_stay_at_the_end_of_the_paragraph) {
    const sh::i18n::BidiLine line = sh::i18n::bidi_line(U"אב  ", Direction::Ltr);
    SH_CHECK(line.order.size() == 4);
    SH_CHECK(line.order[0] == 1 && line.order[1] == 0 && line.order[2] == 2 && line.order[3] == 3);
}

SH_TEST(i18n_bidi_marks_stay_behind_their_base) {
    // bet + dagesh, alef: reversed, the mark is still listed after the letter it belongs to.
    const std::u32string text = U"בּא";
    const sh::i18n::BidiLine line = sh::i18n::bidi_line(text, Direction::Rtl);
    SH_CHECK(line.order.size() == 3);
    SH_CHECK(line.order[0] == 2 && line.order[1] == 0 && line.order[2] == 1);
}

SH_TEST(i18n_bidi_invisible_controls_are_dropped) {
    const sh::i18n::BidiLine line = sh::i18n::bidi_line(U"a\u202Bb\u202Cc", Direction::Ltr);
    SH_CHECK(line.order.size() == 3);
    SH_CHECK(sh::i18n::first_strong(U"123 אב", Direction::Ltr) == Direction::Rtl);
    SH_CHECK(sh::i18n::first_strong(U"123", Direction::Rtl) == Direction::Rtl);
}

SH_TEST(i18n_arabic_letters_take_their_contextual_forms) {
    // beh in the four positions: alone, first, middle, last.
    SH_CHECK(shaped("ب") == "ب");
    SH_CHECK(shaped("ببب") == "ﺑﺒﺐ");
    // alef joins only to the letter before it, so the letter after it starts again.
    SH_CHECK(shaped("باب") == "ﺑﺎب");
    // "سلام": seen initial, lam-alef ligature in its final form, meem alone.
    SH_CHECK(shaped("سلام") == "ﺳﻼم");
    // A space ends the word.
    SH_CHECK(shaped("من هو") == "ﻣﻦ ﻫﻮ");
}

SH_TEST(i18n_arabic_lam_alef_ligatures) {
    SH_CHECK(shaped("لا") == "ﻻ");
    SH_CHECK(shaped("بلا") == "ﺑﻼ");
    SH_CHECK(shaped("لأ") == "ﻷ");
    // The source index of the ligature is the lam; what follows the alef starts a new group.
    const sh::i18n::ShapedText s = sh::i18n::shape_arabic(U"لاب");
    SH_CHECK(s.text.size() == 2 && s.source[0] == 0 && s.source[1] == 2);
    SH_CHECK(s.text[1] == U'ب');
}

SH_TEST(i18n_arabic_marks_and_joiners_do_not_break_a_word) {
    // A vowel mark between two letters leaves them joined.
    SH_CHECK(shaped("بَب") == "ﺑَﺐ");
    // Tatweel joins on both sides; a zero width non-joiner separates.
    SH_CHECK(shaped("بـب") == "ﺑـﺐ");
    SH_CHECK(shaped("ب\u200Cب") == "ب\u200Cب");
    // Other scripts pass through.
    SH_CHECK(shaped("abc שלום") == "abc שלום");
    SH_CHECK(!sh::i18n::has_arabic(U"abc שלום") && sh::i18n::has_arabic(U"a ب"));
    SH_CHECK(sh::i18n::arabic_base(0xFE91) == 0x0628 && sh::i18n::arabic_base(U'a') == U'a');
}
