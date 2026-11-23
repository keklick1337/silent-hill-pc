#include "framework/sh_test.h"

#include "engine/console/console.h"

#include <string>
#include <vector>

using namespace sh::console;

namespace {

struct Rig {
    Registry reg;
    Console con{reg, 5};
    std::vector<std::string> ran;

    Rig() {
        register_builtin_commands(reg);
        con.register_console_commands();
        CommandSpec map;
        map.name = "map";
        map.help = "Load a map";
        ArgSpec a;
        a.name = "name";
        a.type = ArgType::Enum;
        a.values = {"alpha", "alps", "beta"};
        map.args.push_back(a);
        map.run = [this](Context&, const Args& args) {
            ran.push_back("map " + args.str(0));
            return sh::Result<void>::ok();
        };
        (void)reg.add(map);
        CommandSpec maple;
        maple.name = "maple";
        maple.help = "x";
        maple.run = [this](Context&, const Args&) {
            ran.push_back("maple");
            return sh::Result<void>::ok();
        };
        (void)reg.add(maple);
    }
    void type(const std::string& s) { con.on_text(s); }
};

}  // namespace

SH_TEST(console_print_splits_and_trims) {
    Rig r;
    r.con.print(Level::Info, "a\r\nb\nc");
    SH_CHECK(r.con.lines().size() == 3);
    SH_CHECK(r.con.lines()[0].text == "a" && r.con.lines()[2].text == "c");
    const auto rev = r.con.revision();
    r.con.print(Level::Info, "d");
    SH_CHECK(r.con.revision() > rev);
    r.con.print(Level::Info, "e");
    r.con.print(Level::Info, "f");
    SH_CHECK(r.con.lines().size() == 5);
    SH_CHECK(r.con.lines().front().text == "b");
}

SH_TEST(console_popup_opens_on_typing_and_filters) {
    Rig r;
    SH_CHECK(!r.con.popup_open());
    r.type("ma");
    SH_CHECK(r.con.popup_open());
    SH_CHECK(r.con.completion().items.size() == 2);
    r.type("pl");
    SH_CHECK(r.con.completion().items.size() == 1);
    r.con.on_key(Key::Escape);
    SH_CHECK(!r.con.popup_open() && r.con.editor().text() == "mapl");
    r.con.on_key(Key::Escape);
    SH_CHECK(r.con.editor().empty());
}

SH_TEST(console_argument_popup_with_empty_prefix) {
    Rig r;
    r.type("map ");
    SH_CHECK(r.con.popup_open());
    SH_CHECK(r.con.completion().items.size() == 3);
}

SH_TEST(console_down_up_selection_and_enter_accepts) {
    Rig r;
    r.type("map al");
    SH_CHECK(r.con.completion().items.size() == 2);
    r.con.on_key(Key::Down);
    SH_CHECK(r.con.selected() == 0);
    r.con.on_key(Key::Down);
    r.con.on_key(Key::Down);
    SH_CHECK(r.con.selected() == 1);
    r.con.on_key(Key::Up);
    SH_CHECK(r.con.selected() == 0);
    r.con.on_key(Key::Up);
    SH_CHECK(r.con.selected() == 0);
    r.con.on_key(Key::Enter);  // accepts only
    SH_CHECK(r.ran.empty());
    SH_CHECK(r.con.editor().text() == "map alpha");
    r.con.on_key(Key::Enter);  // now runs
    SH_CHECK(r.ran.size() == 1 && r.ran[0] == "map alpha");
    SH_CHECK(r.con.editor().empty());
}

SH_TEST(console_up_from_none_selects_last) {
    Rig r;
    r.type("map ");
    r.con.on_key(Key::Up);
    SH_CHECK(r.con.selected() == 2);
    r.type("b");
    SH_CHECK(r.con.selected() == -1);
}

SH_TEST(console_tab_completes_common_prefix_then_cycles) {
    Rig r;
    r.type("ma");
    r.con.on_key(Key::Tab);  // common prefix of map/maple is "map"
    SH_CHECK(r.con.editor().text() == "map");
    SH_CHECK(r.con.selected() == -1 && r.con.popup_open());
    r.con.on_key(Key::Tab);  // nothing to gain: highlight the first
    SH_CHECK(r.con.selected() == 0);
    r.con.on_key(Key::Tab);
    SH_CHECK(r.con.selected() == 1);
    r.con.on_key(Key::Tab, {false, true, false});
    SH_CHECK(r.con.selected() == 0);
    r.con.on_key(Key::Enter);
    SH_CHECK(r.con.editor().text() == "map ");
    SH_CHECK(r.con.popup_open() && r.con.completion().items.size() == 3);  // arguments offered at once
}

SH_TEST(console_tab_single_candidate_and_empty_prompt) {
    Rig r;
    r.type("mapl");
    r.con.on_key(Key::Tab);
    SH_CHECK(r.con.editor().text() == "maple ");
    Rig e;
    e.con.on_key(Key::Tab);
    SH_CHECK(e.con.popup_open() && e.con.completion().items.size() >= 4);
}

SH_TEST(console_history_browsing_keeps_popup_closed) {
    Rig r;
    r.con.submit_line("map beta");
    r.con.submit_line("echo hi");
    r.type("ma");
    r.con.on_key(Key::Escape);
    r.con.on_key(Key::Up);
    SH_CHECK(r.con.editor().text() == "echo hi" && !r.con.popup_open());
    r.con.on_key(Key::Up);
    SH_CHECK(r.con.editor().text() == "map beta" && !r.con.popup_open());
    r.con.on_key(Key::Down);
    r.con.on_key(Key::Down);
    SH_CHECK(r.con.editor().text() == "ma");
}

SH_TEST(console_submit_echoes_records_and_reports) {
    Rig r;
    bool seen = false;
    bool seen_ok = true;
    r.con.on_executed = [&](std::string_view, bool ok) {
        seen = true;
        seen_ok = ok;
    };
    r.con.set_scroll(7);
    SH_CHECK(r.con.submit_line("map beta"));
    SH_CHECK(r.con.lines().front().text == "> map beta" && r.con.lines().front().level == Level::Command);
    SH_CHECK(seen && seen_ok && r.con.scroll() == 0);
    SH_CHECK(r.con.history().size() == 1);
    SH_CHECK(!r.con.submit_line("nonsense"));
    SH_CHECK(!seen_ok);
}

SH_TEST(console_clear_and_history_commands) {
    Registry reg;
    register_builtin_commands(reg);
    Console c(reg, 100);
    c.register_console_commands();
    c.submit_line("echo one");
    c.submit_line("history");
    bool numbered = false;
    for (const Line& l : c.lines()) numbered |= l.text == "1  echo one";
    SH_CHECK(numbered);
    c.submit_line("history clear");
    SH_CHECK(c.history().size() == 0);
    c.submit_line("clear");
    SH_CHECK(c.lines().empty());
}

SH_TEST(console_selection_text_and_clipboard) {
    Registry reg;
    Console c(reg, 100);
    c.print(Level::Plain, "hello world");
    c.print(Level::Plain, "second");
    c.print(Level::Plain, "third line");
    const auto id0 = c.lines()[0].id;
    c.select_begin({id0 + 2, 3});
    c.select_update({id0, 6});  // reversed on purpose
    SH_CHECK(c.has_selection());
    SH_CHECK(c.selected_text() == "world\nsecond\nthi");
    std::string clip;
    c.clipboard_set = [&](const std::string& s) { clip = s; };
    c.select_begin({id0, 6});
    c.select_update({id0, 99});  // clamped to the line length
    c.on_ctrl_char('c');
    SH_CHECK(clip == "world");
    c.select_clear();
    SH_CHECK(!c.has_selection() && c.selected_text().empty());
}

SH_TEST(console_select_word_line_and_copy) {
    Registry reg;
    Console c(reg, 100);
    c.print(Level::Info, "dump 20261120-101500-a1b2 written to /tmp/x y.txt");
    c.print(Level::Plain, "");
    const auto id = c.lines()[0].id;
    std::string clip;
    int sets = 0;
    c.clipboard_set = [&](const std::string& s) { clip = s; ++sets; };
    SH_CHECK(!c.copy_selection());  // nothing selected: silent, clipboard untouched
    SH_CHECK(sets == 0 && c.lines().size() == 2);
    c.select_word({id, 8});  // inside the dump id: dashes belong to the word
    SH_CHECK(c.selected_text() == "20261120-101500-a1b2");
    c.select_word({id, 4});  // the space between words
    SH_CHECK(c.selected_text() == " ");
    c.select_word({id, 37});  // path punctuation is its own run
    SH_CHECK(c.selected_text() == "/");
    c.select_word({id, 999});  // past the end: the last run
    SH_CHECK(c.selected_text() == "txt");
    c.select_word({id + 1, 0});  // empty line: nothing to pick, the selection stays
    SH_CHECK(c.selected_text() == "txt");
    c.select_line({id, 5});
    SH_CHECK(c.selected_text() == c.lines()[0].text);
    SH_CHECK(c.copy_selection());
    SH_CHECK(clip == c.lines()[0].text && sets == 1);
    // the copy is acknowledged in the log and the selection survives it
    SH_CHECK(c.lines().size() == 3 && c.lines().back().text == "copied 49 bytes, 1 line");
    SH_CHECK(c.has_selection());
    c.on_ctrl_char('c');
    SH_CHECK(sets == 2);
}

SH_TEST(console_ctrl_keys_and_paste) {
    Rig r;
    r.con.clipboard_get = [] { return std::string("map beta\nignored"); };
    r.con.on_ctrl_char('v');
    SH_CHECK(r.con.editor().text() == "map beta");
    r.con.on_ctrl_char('a');
    SH_CHECK(r.con.editor().caret() == 0);
    r.con.on_ctrl_char('e');
    SH_CHECK(r.con.editor().caret() == 8);
    r.con.on_ctrl_char('w');
    SH_CHECK(r.con.editor().text() == "map ");
    r.con.on_ctrl_char('u');
    SH_CHECK(r.con.editor().empty());
    r.con.print(Level::Plain, "x");
    r.con.on_ctrl_char('l');
    SH_CHECK(r.con.lines().empty());
}

SH_TEST(console_ctrl_arrows_move_words_and_scroll_keys) {
    Rig r;
    r.type("echo one two");
    r.con.on_key(Key::Left, {true, false, false});
    SH_CHECK(r.con.editor().caret() == 9);
    r.con.on_key(Key::Home);
    SH_CHECK(r.con.editor().caret() == 0);
    r.con.on_key(Key::PageUp);
    SH_CHECK(r.con.scroll() == 10);
    r.con.on_wheel(-3);
    SH_CHECK(r.con.scroll() == 7);
    r.con.on_key(Key::PageDown);
    r.con.on_key(Key::PageDown);
    SH_CHECK(r.con.scroll() == 0);
    r.con.set_scroll(5);
    r.con.on_key(Key::Escape);
    r.con.on_key(Key::End);
    SH_CHECK(r.con.scroll() == 0);
}
