// SPDX-License-Identifier: GPL-3.0-or-later
// gamedata/menu/controls_screen.json carries the built-in layout of the PC controls screen,
// bad files are refused, a mod file wins; the tab rows, hit tests and focus order.

#include <filesystem>
#include <fstream>
#include <string>

#include "engine/screens/controls_layout.h"
#include "framework/sh_test.h"
#include "framework/mod_fixture.h"

using namespace sh::screens;

namespace {

bool rgb_is(const std::array<int, 3>& c, int r, int g, int b) { return c[0] == r && c[1] == g && c[2] == b; }

void check_builtin(const ControlsScreenLayout& a) {
    SH_CHECK(a.tabs_y == 4 && a.tab_x[0] == 72 && a.tab_x[1] == 160 && a.tab_x[2] == 248 && a.tab_hit_pad == 6);
    SH_CHECK(a.rule1_y == 23 && a.head_y == 25 && a.rule2_y == 42);
    SH_CHECK(a.arrow_up_y == 49 && a.list_y == 56 && a.row_pitch == 16 && a.visible_rows == 8);
    SH_CHECK(a.arrow_down_y == 189 && a.rule3_y == 198 && a.footer_y == 202 && a.confirm_y == 96);
    SH_CHECK(a.rule_x0 == 4 && a.rule_x1 == 316 && a.text_x == 28 && a.value_right == 300 && a.label_w == 126);
    SH_CHECK(a.highlight_dy == 14 && a.arrow_x == 160);
    SH_CHECK(a.row_h == 16 && a.icon_dy == 0 && a.icon_layer == 7 && a.icon_suffix_dx == 1 && a.icon_text_dx == 3);
    SH_CHECK(a.icon_origin_x == 160 && a.icon_origin_y == 112);
    SH_CHECK(a.bullet_highlight_left == 1 && a.bullet_highlight_right == 4);
    SH_CHECK(a.footer_hit_right == 4 && a.cell_hit_pad == 2 && a.icon_arrow_hit_w == 16);
    SH_CHECK(a.confirm_hit_dy == 24 && a.confirm_hit_x0 == 80 && a.confirm_hit_x1 == 240 && a.confirm_split_x == 160);
    SH_CHECK(a.beep_volume == 64);
    SH_CHECK(a.columns[0].x == 156 && a.columns[0].w == 82 && a.columns[1].x == 240 && a.columns[1].w == 76);
    // Menu font slots: kGold 0, kDarkGrey 1, kRed 4, kLightGrey 6, kWhite 7.
    const ControlsScreenColors& c = a.colors;
    SH_CHECK(c.selected == 0 && c.disabled == 1 && c.duplicate == 4 && c.normal == 7);
    SH_CHECK(c.heading == 6 && c.heading_selected == 0 && c.stick == 1 && c.label == 7 && c.value == 7);
    SH_CHECK(c.other_device == 1);
    SH_CHECK(rgb_is(a.tints.normal, 128, 128, 128) && rgb_is(a.tints.disabled, 64, 64, 64) &&
             rgb_is(a.tints.duplicate, 128, 0, 0));
    const ControlsScreenTexts& t = a.texts;
    SH_CHECK(t.tabs[0] == "ui.controls_screen.keyboard" && t.tabs[1] == "ui.controls_screen.mouse" &&
             t.tabs[2] == "ui.controls_screen.gamepad");
    SH_CHECK(t.primary == "ui.controls_screen.primary" && t.secondary == "ui.controls_screen.secondary");
    SH_CHECK(t.gamepad_icons == "ui.controls_screen.gamepad_icons" && t.action_prefix == "ui.bind.");
    SH_CHECK(t.back == "ui.controls_screen.back" && t.reset == "ui.controls_screen.reset" &&
             t.reset_controls == "ui.controls_screen.reset_controls");
    SH_CHECK(t.listening == "..." && t.unbound == "-");
    SH_CHECK(t.stick_rows[0] == "action_move_forward" && t.stick_rows[1] == "action_turn_right" &&
             t.stick_rows[2] == "action_move_back" && t.stick_rows[3] == "action_turn_left");
}

std::filesystem::path scratch(const char* name) {
    const std::filesystem::path dir = std::filesystem::path(SH_PROJECT_ROOT) / ".tmp" / "screens_test" / name;
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void write(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
    sh::test::mod_file_written(file);
}

// Shaped like the bind table (input/bind/bind_table.h): Move rows (stick), a pad+mouse row, a keyboard-only
// row, a row with a keyboard and a pad slot but no mouse, a keyboard-only global row.
std::vector<ControlsRowCaps> sample_rows() {
    return {
        {true, true, false, true},    // 0 Move Up
        {true, true, true, false},    // 1 Sidestep Left
        {true, false, false, false},  // 2 Quick Save
        {true, false, true, false},   // 3 Flashlight
        {true, true, true, false},    // 4 Swap Shoulder
    };
}

}  // namespace

SH_TEST(controls_layout_json_matches_builtin) {
    const auto loaded = load_controls_layout(std::filesystem::path(SH_PROJECT_ROOT) / "gamedata", {}, nullptr);
    SH_CHECK(static_cast<bool>(loaded));
    if (loaded) {
        check_builtin(loaded.value());
    }
}

SH_TEST(controls_layout_builtin_default) {
    check_builtin(ControlsScreenLayout{});
    const auto empty = parse_controls_layout("{}");
    SH_CHECK(static_cast<bool>(empty));
    if (empty) {
        check_builtin(empty.value());
    }
}

SH_TEST(controls_layout_refuses_bad_files) {
    SH_CHECK(!parse_controls_layout("[]"));
    SH_CHECK(!parse_controls_layout("{\"list_y\": \"56\"}"));
    SH_CHECK(!parse_controls_layout("{\"row_pitch\": 0}"));
    SH_CHECK(!parse_controls_layout("{\"no_such_key\": 1}"));
    SH_CHECK(!parse_controls_layout("{\"columns\": [[134, 48]]}"));
    SH_CHECK(!parse_controls_layout("{\"columns\": [[134, 48], [182, 0]]}"));
    SH_CHECK(!parse_controls_layout("{\"tab_x\": [1, 2]}"));
    SH_CHECK(!parse_controls_layout("{\"tab_x\": [1, 2, \"3\"]}"));
    SH_CHECK(!parse_controls_layout("{\"colors\": {\"selected\": \"purple\"}}"));
    SH_CHECK(!parse_controls_layout("{\"colors\": {\"nope\": \"gold\"}}"));
    SH_CHECK(!parse_controls_layout("{\"icon_tints\": {\"normal\": [128, 128]}}"));
    SH_CHECK(!parse_controls_layout("{\"icon_tints\": {\"normal\": [128, 128, 256]}}"));
    SH_CHECK(!parse_controls_layout("{\"texts\": {\"back\": \"\"}}"));
    SH_CHECK(!parse_controls_layout("{\"texts\": {\"tabs\": [\"a\", \"b\"]}}"));
    SH_CHECK(!parse_controls_layout("{\"texts\": {\"tabs\": [\"a\", \"\", \"c\"]}}"));
    SH_CHECK(!parse_controls_layout("{\"texts\": {\"stick_rows\": [\"a\", \"b\", \"c\"]}}"));
    SH_CHECK(!parse_controls_layout("{\"texts\": {\"unknown\": \"x\"}}"));
    const auto ok = parse_controls_layout(
        "{\"_note\": 1, \"list_y\": 60, \"tab_x\": [10, 20, 30], \"colors\": {\"selected\": \"green\"}, "
        "\"texts\": {\"unbound\": \"\", \"tabs\": [\"k\", \"m\", \"g\"]}}");
    SH_CHECK(static_cast<bool>(ok));
    if (ok) {
        SH_CHECK(ok.value().list_y == 60 && ok.value().colors.selected == 2 && ok.value().texts.unbound.empty());
        SH_CHECK(ok.value().tab_x[2] == 30 && ok.value().texts.tabs[1] == "m");
        SH_CHECK(ok.value().row_pitch == 16);
    }
}

SH_TEST(controls_layout_mod_file_wins) {
    const std::filesystem::path root = scratch("controls");
    const std::filesystem::path gamedata = root / "gamedata";
    const std::filesystem::path mods = root / "mods";
    write(gamedata / "menu" / "controls_screen.json", "{\"list_y\": 56}");
    write(mods / "mymod" / "mod.json", R"({"id": "mymod", "version": "1.0.0"})");
    write(mods / "mymod" / "menu" / "controls_screen.json", "{\"list_y\": 70}");
    std::string source;
    const auto loaded = load_controls_layout(gamedata, mods, &source);
    SH_CHECK(static_cast<bool>(loaded));
    if (loaded) {
        SH_CHECK(loaded.value().list_y == 70);
    }
    SH_CHECK(source.find("mymod") != std::string::npos);
    write(mods / "mymod" / "menu" / "controls_screen.json", "{\"list_y\": true}");
    SH_CHECK(!load_controls_layout(gamedata, mods, nullptr));
    std::filesystem::remove_all(root);
}

SH_TEST(controls_layout_tab_rows) {
    const std::vector<ControlsRowCaps> rows = sample_rows();
    SH_CHECK((controls_tab_rows(ControlsTab::Keyboard, rows) == std::vector<int>{0, 1, 2, 3, 4}));
    SH_CHECK((controls_tab_rows(ControlsTab::Mouse, rows) == std::vector<int>{0, 1, 4}));
    // The Move rows stay on the pad tab: their stick and d-pad are shown, not bound.
    SH_CHECK((controls_tab_rows(ControlsTab::Gamepad, rows) == std::vector<int>{0, 1, 3, 4}));
    SH_CHECK(controls_tab_rows(ControlsTab::Mouse, {}).empty());
}

SH_TEST(controls_layout_tab_step_wraps) {
    SH_CHECK(controls_tab_step(ControlsTab::Keyboard, 1) == ControlsTab::Mouse);
    SH_CHECK(controls_tab_step(ControlsTab::Gamepad, 1) == ControlsTab::Keyboard);
    SH_CHECK(controls_tab_step(ControlsTab::Keyboard, -1) == ControlsTab::Gamepad);
    SH_CHECK(controls_tab_step(ControlsTab::Mouse, -1) == ControlsTab::Keyboard);
}

SH_TEST(controls_layout_tab_hit) {
    const ControlsScreenLayout l;
    const std::array<int, kControlsTabs> w = {60, 40, 64};
    SH_CHECK(controls_tab_left(l, 0, 60) == 42);
    SH_CHECK(controls_tab_hit(l, w, 72, 10) == 0);
    SH_CHECK(controls_tab_hit(l, w, 42 - 6, 4) == 0);         // the pad counts
    SH_CHECK(controls_tab_hit(l, w, 42 - 7, 4) == -1);
    SH_CHECK(controls_tab_hit(l, w, 160, 19) == 1);
    SH_CHECK(controls_tab_hit(l, w, 160, 20) == -1);          // below the row
    SH_CHECK(controls_tab_hit(l, w, 160, 3) == -1);           // above it
    SH_CHECK(controls_tab_hit(l, w, 248 + 32 + 5, 8) == 2);
    SH_CHECK(controls_tab_hit(l, w, 115, 8) == -1);           // between tabs
}

SH_TEST(controls_layout_slot_and_list_hit) {
    const ControlsScreenLayout l;
    SH_CHECK(controls_slot_hit(l, 153) == -1);
    SH_CHECK(controls_slot_hit(l, 154) == 0);
    SH_CHECK(controls_slot_hit(l, 237) == 0);
    SH_CHECK(controls_slot_hit(l, 238) == 1);
    SH_CHECK(controls_slot_hit(l, 317) == 1);
    SH_CHECK(controls_slot_hit(l, 318) == -1);
    SH_CHECK(controls_list_hit(l, 0, 8, 20, 55) == -1);
    SH_CHECK(controls_list_hit(l, 0, 8, 20, 56) == 0);
    SH_CHECK(controls_list_hit(l, 3, 8, 20, 56 + 16 * 2 + 5) == 5);
    SH_CHECK(controls_list_hit(l, 0, 8, 20, 56 + 16 * 8) == -1);  // past the visible rows
    SH_CHECK(controls_list_hit(l, 0, 8, 3, 56 + 16 * 3) == -1);   // past the tab's rows
}

SH_TEST(controls_layout_focus_order) {
    using F = ControlsFocus;
    auto mv = [](F f, int row, int dir, bool icons, int count) {
        return controls_move_vertical(ControlsCursor{f, row}, dir, icons, count);
    };
    // Keyboard: tabs -> list -> footer -> tabs.
    SH_CHECK(mv(F::Tabs, 0, 1, false, 5).focus == F::List && mv(F::Tabs, 0, 1, false, 5).row == 0);
    SH_CHECK(mv(F::List, 2, 1, false, 5).row == 3);
    SH_CHECK(mv(F::List, 4, 1, false, 5).focus == F::Back);
    SH_CHECK(mv(F::Back, 0, 1, false, 5).focus == F::Tabs);
    SH_CHECK(mv(F::Reset, 0, 1, false, 5).focus == F::Tabs);
    SH_CHECK(mv(F::Tabs, 0, -1, false, 5).focus == F::Back);
    SH_CHECK(mv(F::Back, 0, -1, false, 5).focus == F::List && mv(F::Back, 0, -1, false, 5).row == 4);
    SH_CHECK(mv(F::List, 0, -1, false, 5).focus == F::Tabs);
    // Gamepad: the icons row sits between the tabs and the list.
    SH_CHECK(mv(F::Tabs, 0, 1, true, 5).focus == F::Icons);
    SH_CHECK(mv(F::Icons, 0, 1, true, 5).focus == F::List);
    SH_CHECK(mv(F::Icons, 0, -1, true, 5).focus == F::Tabs);
    SH_CHECK(mv(F::List, 0, -1, true, 5).focus == F::Icons);
    // An empty list is skipped.
    SH_CHECK(mv(F::Tabs, 0, 1, false, 0).focus == F::Back);
    SH_CHECK(mv(F::Back, 0, -1, false, 0).focus == F::Tabs);
    SH_CHECK(mv(F::Back, 0, -1, true, 0).focus == F::Icons);
}

SH_TEST(controls_layout_label_slug) {
    SH_CHECK(controls_label_slug("Back / Cancel") == "back_cancel");
    SH_CHECK(controls_label_slug("Move Up") == "move_up");
    SH_CHECK(controls_label_slug("Debug Options") == "debug_options");
    SH_CHECK(controls_label_slug("  Map ") == "map");
}
