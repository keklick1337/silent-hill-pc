// Strings made ready for the drawer (src/i18n/core/text_prepare.h): untouched when nothing applies, visual order
// and shaping with the markup kept in place, colours following their characters, and line wrapping of messages
// for text with spaces, text without them and wide characters.

#include "i18n/core/text_prepare.h"
#include "i18n/core/unicode.h"

#include "framework/sh_test.h"

#include <string>

namespace {

using sh::i18n::Direction;
using sh::i18n::PrepareOptions;
using sh::i18n::PreparedText;
using sh::i18n::TextDialect;

// Every character 10 pixels wide, ideographs 16.
int flat_advance(char32_t cp) { return sh::i18n::is_wide(cp) ? 16 : 10; }

PrepareOptions string_options(Direction base) {
    PrepareOptions o;
    o.dialect = TextDialect::String;
    o.base = base;
    return o;
}

PrepareOptions message_options(Direction base, int width) {
    PrepareOptions o;
    o.dialect = TextDialect::Message;
    o.base = base;
    o.wrap_width = width;
    o.inset_wrap_width = width / 2;
    o.advance = flat_advance;
    return o;
}

}  // namespace

SH_TEST(i18n_prepare_leaves_latin_and_cyrillic_strings_alone) {
    SH_CHECK(!sh::i18n::prepare_text("Aspect_Ratio", string_options(Direction::Ltr)).changed);
    SH_CHECK(!sh::i18n::prepare_text("\x07PAUSED\n\x02second_line", string_options(Direction::Ltr)).changed);
    SH_CHECK(!sh::i18n::prepare_text("Соотношение_сторон_é", string_options(Direction::Ltr)).changed);
    // Messages that fit their lines, markup and layout bytes included.
    const char* msg = "~J0(2.5)\tWas_I_dreaming? ~N\n\t\t\tNo. ~E ";
    SH_CHECK(!sh::i18n::prepare_text(msg, message_options(Direction::Ltr, 304)).changed);
    const char* pick = "\tThere_is_a_ ~C2 Kitchen_knife ~C7 . ~N\n\tTake_it? ~S4 ";
    SH_CHECK(!sh::i18n::prepare_text(pick, message_options(Direction::Ltr, 304)).changed);
}

SH_TEST(i18n_prepare_composes_a_letter_with_its_mark) {
    const PreparedText p = sh::i18n::prepare_text("cafe\xCC\x81", string_options(Direction::Ltr));
    SH_CHECK(p.changed && p.text == "café");
}

SH_TEST(i18n_prepare_orders_a_right_to_left_string) {
    // Hebrew in a left-to-right language: the word is reversed in place.
    PreparedText p = sh::i18n::prepare_text("abc_שלום_def", string_options(Direction::Ltr));
    SH_CHECK(p.changed && p.text == "abc_םולש_def");
    // A right-to-left language: the whole line, brackets mirrored; '_' stays the drawer's space.
    p = sh::i18n::prepare_text("אב_(גד)", string_options(Direction::Rtl));
    SH_CHECK(p.changed && p.text == "(דג)_בא");
    // Each line of a string is ordered on its own.
    p = sh::i18n::prepare_text("אב\nגד", string_options(Direction::Rtl));
    SH_CHECK(p.changed && p.text == "בא\nדג");
    // English in a right-to-left language keeps its letters in order; the closing period moves to the left end.
    p = sh::i18n::prepare_text("Display_Mode.", string_options(Direction::Rtl));
    SH_CHECK(p.changed && p.text == ".Display_Mode");
    SH_CHECK(!sh::i18n::prepare_text("1280x960", string_options(Direction::Rtl)).changed);
}

SH_TEST(i18n_prepare_shapes_arabic_and_writes_it_right_to_left) {
    // "سلام" shaped is seen-initial, lam-alef, meem; drawn from the right, so written reversed.
    const PreparedText p = sh::i18n::prepare_text("سلام", string_options(Direction::Rtl));
    SH_CHECK(p.changed && p.text == "مﻼﺳ");
}

SH_TEST(i18n_prepare_keeps_colours_on_their_characters) {
    // Written: white "אב", gold "גד". Drawn from the left the gold word comes first, and the string still ends in
    // the colour it ended in as written.
    PreparedText p = sh::i18n::prepare_text("\x07" "אב" "\x02" "גד", string_options(Direction::Rtl));
    SH_CHECK(p.changed && p.text == "\x02" "דג" "\x07" "בא" "\x02");
    // The same in a message, with the codes of the line kept before and after its text.
    p = sh::i18n::prepare_text("~J0(1.5)\t~C7 אב ~C2 גד ~E ", message_options(Direction::Rtl, 0));
    SH_CHECK(p.changed && p.text == "~J0(1.5)\t~C2דג~C7בא~C2~E ");
}

SH_TEST(i18n_prepare_gives_uncoloured_text_its_colour_back) {
    // Written: plain words, then a gold span, then plain again. Drawn from the left, the gold span comes before the
    // first words, which were written before any colour code: knowing the colour the message starts in (white),
    // they get it back.
    PrepareOptions o = message_options(Direction::Rtl, 0);
    o.initial_color = 7;
    PreparedText p = sh::i18n::prepare_text("אב_ ~C2 גד ~C7 _הו ~E ", o);
    SH_CHECK(p.changed && p.text == "וה_~C2דג~C7_בא~E ");
    // Not knowing it, nothing can be written for them: the string stays in its last colour.
    o.initial_color = -1;
    p = sh::i18n::prepare_text("אב_ ~C2 גד ~C7 _הו ~E ", o);
    SH_CHECK(p.changed && p.text == "~C7וה_~C2דג_בא~C7~E ");
    // A message of one colour needs no code at all.
    o.initial_color = 7;
    p = sh::i18n::prepare_text("אב_גד ~E ", o);
    SH_CHECK(p.changed && p.text == "דג_בא~E ");
}

SH_TEST(i18n_prepare_keeps_kerning_bytes_with_their_letter) {
    const PreparedText p = sh::i18n::prepare_text("א\x01" "ב", string_options(Direction::Rtl));
    SH_CHECK(p.changed && p.text == "בא\x01");
}

SH_TEST(i18n_prepare_wraps_a_message_at_spaces) {
    // 10 px a letter, 6 a space, 60 px lines: "aaa bbb" is 66 wide and breaks at the space, which is dropped.
    PreparedText p = sh::i18n::prepare_text("aaa_bbb_cc ~E ", message_options(Direction::Ltr, 60));
    SH_CHECK(p.changed && p.wrapped == 1);
    SH_CHECK(p.text == "aaa~N\nbbb_cc~E ");
    // The codes before the text stay on the first line, the closing code on the last.
    p = sh::i18n::prepare_text("~J0(2.0)\taaa_bbb_ccc_ddd ~E ", message_options(Direction::Ltr, 60));
    SH_CHECK(p.wrapped == 3 && p.text == "~J0(2.0)\taaa~N\nbbb~N\nccc~N\nddd~E ");
    // Lines the author broke stay as they are when they fit.
    SH_CHECK(!sh::i18n::prepare_text("aaa_bb ~N\n\tccc ~E ", message_options(Direction::Ltr, 60)).changed);
}

SH_TEST(i18n_prepare_wraps_a_word_wider_than_the_line) {
    const PreparedText p = sh::i18n::prepare_text("abcdefghij ~E ", message_options(Direction::Ltr, 40));
    SH_CHECK(p.wrapped == 2 && p.text == "abcd~N\nefgh~N\nij~E ");
}

SH_TEST(i18n_prepare_wraps_text_without_spaces_between_any_two_ideographs) {
    // 16 px each, 50 px lines: three to a line.
    PreparedText p = sh::i18n::prepare_text("一二三四五六七 ~E ", message_options(Direction::Ltr, 50));
    SH_CHECK(p.wrapped == 2 && p.text == "一二三~N\n四五六~N\n七~E ");
    // A line does not start with closing punctuation: the break moves one character back.
    p = sh::i18n::prepare_text("一二三。四 ~E ", message_options(Direction::Ltr, 50));
    SH_CHECK(p.wrapped == 1 && p.text == "一二~N\n三。四~E ");
    // Nor end with an opening bracket.
    p = sh::i18n::prepare_text("一二「三 ~E ", message_options(Direction::Ltr, 50));
    SH_CHECK(p.text == "一二~N\n「三~E ");
    // A Latin word inside stays whole.
    p = sh::i18n::prepare_text("一二abc三四 ~E ", message_options(Direction::Ltr, 50));
    SH_CHECK(p.text == "一二~N\nabc三~N\n四~E ");
}

SH_TEST(i18n_prepare_wraps_right_to_left_text_in_reading_order) {
    // Two words to a line; each line is then written right to left.
    const PreparedText p = sh::i18n::prepare_text("אבג_דהו_זחט ~E ", message_options(Direction::Rtl, 70));
    SH_CHECK(p.wrapped == 1 && p.text == "והד_גבא~N\nטחז~E ");
}

SH_TEST(i18n_prepare_inset_lines_wrap_narrower) {
    // After ~T lines start at the inset: the narrower width applies from that line on.
    const PreparedText p = sh::i18n::prepare_text("~T aaa_bbb ~E ", message_options(Direction::Ltr, 80));
    SH_CHECK(p.wrapped == 1 && p.text == "~T aaa~N\nbbb~E ");
    SH_CHECK(!sh::i18n::prepare_text("aaa_bbb ~E ", message_options(Direction::Ltr, 80)).changed);
}

SH_TEST(i18n_prepare_drops_invisible_characters) {
    // A zero width space is a place to break and draws nothing.
    PreparedText p = sh::i18n::prepare_text("aaa\xE2\x80\x8B" "bbb ~E ", message_options(Direction::Ltr, 40));
    SH_CHECK(p.text == "aaa~N\nbbb~E ");
    p = sh::i18n::prepare_text("a\xE2\x80\x8E" "b", string_options(Direction::Ltr));
    SH_CHECK(p.changed && p.text == "ab");
}

SH_TEST(i18n_prepare_reads_the_bytes_the_font_gives_other_meanings) {
    SH_CHECK(sh::i18n::drawn_character('[') == U'“' && sh::i18n::drawn_character('=') == U'—');
    SH_CHECK(sh::i18n::drawn_character('a') == U'a');
    // '[' ']' draw quotation marks, which do not mirror; real parentheses do.
    const PreparedText p = sh::i18n::prepare_text("[אב]_(גד)", string_options(Direction::Rtl));
    SH_CHECK(p.changed && p.text == "(דג)_]בא[");
}

SH_TEST(i18n_prepare_plain_text_for_the_overlays) {
    PrepareOptions o;
    o.dialect = TextDialect::Plain;
    o.base = Direction::Rtl;
    // Real spaces are spaces, '_' and '[' are themselves, and square brackets mirror.
    PreparedText p = sh::i18n::prepare_text("אב [גד] a_b", o);
    SH_CHECK(p.changed && p.text == "a_b [דג] בא");
    p = sh::i18n::prepare_text("سلام", o);
    SH_CHECK(p.changed && p.text == "مﻼﺳ");
    o.base = Direction::Ltr;
    SH_CHECK(!sh::i18n::prepare_text("Reset all PC options to their defaults?", o).changed);
    SH_CHECK(!sh::i18n::prepare_text("Сбросить все настройки?", o).changed);
}
