#include "framework/sh_test.h"

#include "engine/console/line_editor.h"

using sh::console::LineEditor;

SH_TEST(line_editor_insert_filters_and_stops_at_newline) {
    LineEditor e;
    SH_CHECK(e.insert("ab\x01\x7f" "c") == 3);
    SH_CHECK(e.text() == "abc" && e.caret() == 3);
    SH_CHECK(e.insert("x\ny") == 1);
    SH_CHECK(e.text() == "abcx");
    e.home();
    e.insert("12");
    SH_CHECK(e.text() == "12abcx" && e.caret() == 2);
}

SH_TEST(line_editor_utf8_caret_stays_on_boundaries) {
    LineEditor e;
    e.set("a\xD0\xBF\xE2\x82\xAC" "z");  // a, 2-byte, 3-byte, z
    SH_CHECK(e.caret() == 7);
    e.left(); SH_CHECK(e.caret() == 6);
    e.left(); SH_CHECK(e.caret() == 3);
    e.left(); SH_CHECK(e.caret() == 1);
    e.right(); SH_CHECK(e.caret() == 3);
    e.set(e.text(), 4);  // inside the euro sign
    SH_CHECK(e.caret() == 3);
    e.set(e.text(), 99);
    SH_CHECK(e.caret() == 7);
    e.left(); e.left();
    e.backspace();  // removes the 2-byte char
    SH_CHECK(e.text() == "a\xE2\x82\xAC" "z" && e.caret() == 1);
    e.del();
    SH_CHECK(e.text() == "az");
    e.end(); e.del();
    SH_CHECK(e.text() == "az");
    e.home(); e.backspace();
    SH_CHECK(e.text() == "az" && e.caret() == 0);
    SH_CHECK(LineEditor::next_boundary("\xD0\xBF", 0) == 2);
    SH_CHECK(LineEditor::prev_boundary("\xD0\xBF", 2) == 0);
    SH_CHECK(LineEditor::next_boundary("a", 5) == 1);
}

SH_TEST(line_editor_word_movement_and_kills) {
    LineEditor e;
    e.set("foo_1 bar  baz");
    e.word_left(); SH_CHECK(e.caret() == 11);
    e.word_left(); SH_CHECK(e.caret() == 6);
    e.word_left(); SH_CHECK(e.caret() == 0);
    e.word_left(); SH_CHECK(e.caret() == 0);
    e.word_right(); SH_CHECK(e.caret() == 5);
    e.word_right(); SH_CHECK(e.caret() == 9);
    e.word_right(); e.word_right(); SH_CHECK(e.caret() == 14);

    e.set("set  value.x");
    e.kill_word_back();
    SH_CHECK(e.text() == "set  value." && e.caret() == 11);
    e.kill_word_back();
    SH_CHECK(e.text() == "set  " && e.caret() == 5);
    e.kill_word_back();
    SH_CHECK(e.text().empty());

    e.set("hello world", 5);
    e.kill_to_end();
    SH_CHECK(e.text() == "hello");
    e.set("hello world", 6);
    e.kill_to_start();
    SH_CHECK(e.text() == "world" && e.caret() == 0);
    e.clear();
    SH_CHECK(e.empty() && e.caret() == 0);
}
