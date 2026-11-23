// SPDX-License-Identifier: GPL-3.0-or-later
// Layout of the PC controls (rebind) screen in the 320x240 text space, its colours, text keys
// and cell texts as data: gamedata/menu/controls_screen.json, mod override by file name,
// built-in default when the file is missing or refused. Omitted keys keep the built-in value.
// Also the screen's pure layout math: which bind rows each device tab lists, the tab and
// cell hit tests and the up/down focus order.
#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::screens {

// Device tabs, left to right.
enum class ControlsTab { Keyboard, Mouse, Gamepad };
inline constexpr int kControlsTabs = 3;
// Bind slots per device: primary and secondary.
inline constexpr int kControlsSlots = 2;

struct ControlsColumn {
    int x = 0;
    int w = 0;
};

// Menu font palette slots (save_layout.h names them).
struct ControlsScreenColors {
    int selected = 0, disabled = 1, duplicate = 4, normal = 7;
    int heading = 6, heading_selected = 0, stick = 1, label = 7, value = 7;
    int other_device = 1;  // a slot holding the other device's input (a key on the Mouse tab)
};

// Tint of an icon cell (0..255 per channel, 128 = unlit).
struct ControlsGlyphTints {
    std::array<int, 3> normal = {128, 128, 128};
    std::array<int, 3> disabled = {64, 64, 64};
    std::array<int, 3> duplicate = {128, 0, 0};
};

struct ControlsScreenTexts {
    std::array<std::string, kControlsTabs> tabs = {"ui.controls_screen.keyboard", "ui.controls_screen.mouse",
                                                  "ui.controls_screen.gamepad"};
    std::string primary = "ui.controls_screen.primary";
    std::string secondary = "ui.controls_screen.secondary";
    std::string gamepad_icons = "ui.controls_screen.gamepad_icons";
    std::string back = "ui.controls_screen.back";
    std::string reset = "ui.controls_screen.reset";
    std::string reset_controls = "ui.controls_screen.reset_controls";
    // Text key of a row's label: this prefix and the label as a slug ("Back / Cancel" ->
    // back_cancel).
    std::string action_prefix = "ui.bind.";
    // Drawn as they are (menu font).
    std::string listening = "...";
    std::string unbound = "-";
    // The Move rows' first keyboard binds, in up, right, down, left order.
    std::array<std::string, 4> stick_rows = {"action_move_forward", "action_turn_right", "action_move_back",
                                             "action_turn_left"};
};

struct ControlsScreenLayout {
    int tabs_y = 4;
    std::array<int, kControlsTabs> tab_x = {72, 160, 248};  // tab label centres
    int tab_hit_pad = 6;
    int rule1_y = 23, head_y = 25, rule2_y = 42;
    int arrow_up_y = 49, list_y = 56, row_pitch = 16, visible_rows = 8, arrow_down_y = 189;
    int rule3_y = 198, footer_y = 202, confirm_y = 96;
    int rule_x0 = 4, rule_x1 = 316;
    int text_x = 28, value_right = 300, label_w = 126;
    int highlight_dy = 14;
    int arrow_x = 160;
    int row_h = 16;
    int icon_dy = 0, icon_layer = 7, icon_suffix_dx = 1, icon_text_dx = 3;
    int icon_origin_x = 160, icon_origin_y = 112;
    int bullet_highlight_left = 1, bullet_highlight_right = 4;
    int footer_hit_right = 4, cell_hit_pad = 2, icon_arrow_hit_w = 16;
    int confirm_hit_dy = 24, confirm_hit_x0 = 80, confirm_hit_x1 = 240, confirm_split_x = 160;
    int beep_volume = 64;
    // Primary and secondary slot of the tab's device.
    std::array<ControlsColumn, kControlsSlots> columns = {{{156, 82}, {240, 76}}};

    ControlsScreenColors colors;
    ControlsGlyphTints tints;
    ControlsScreenTexts texts;
};

Result<ControlsScreenLayout> parse_controls_layout(std::string_view json_text);
// `source` (optional) receives the file read. Mods are searched newest-first under mods_dir.
Result<ControlsScreenLayout> load_controls_layout(const std::filesystem::path& gamedata_dir,
                                                  const std::filesystem::path& mods_dir, std::string* source);

// ---- layout math ----------------------------------------------------------------------

// What a bind row can take (the row's flags and key columns of input/bind/bind_table.h).
struct ControlsRowCaps {
    bool keyboard = false;  // has a keyboard slot
    bool mouse = false;     // its keyboard slots take mouse buttons
    bool pad = false;       // has a pad slot
    bool stick = false;     // its pad side is the stick / d-pad (shown, not bindable)
};

// The bind rows a tab lists, in table order.
std::vector<int> controls_tab_rows(ControlsTab tab, const std::vector<ControlsRowCaps>& rows);
ControlsTab controls_tab_step(ControlsTab tab, int dir);  // wraps
// Left x of a tab label `width` wide.
int controls_tab_left(const ControlsScreenLayout& l, int tab, int width);
// The tab under (mx, my) given each label's width, or -1.
int controls_tab_hit(const ControlsScreenLayout& l, const std::array<int, kControlsTabs>& widths, int mx, int my);
// The slot column under mx, or -1.
int controls_slot_hit(const ControlsScreenLayout& l, int mx);
// The list index under my (top = first visible, count = rows of the tab), or -1.
int controls_list_hit(const ControlsScreenLayout& l, int top, int visible, int count, int my);

// Up/down focus order: tabs, the Gamepad Icons row (Gamepad tab only), the list,
// the footer (Back / Reset), and around to the tabs.
enum class ControlsFocus { Tabs, Icons, List, Back, Reset };
struct ControlsCursor {
    ControlsFocus focus = ControlsFocus::List;
    int row = 0;  // list index while focus is List
};
ControlsCursor controls_move_vertical(ControlsCursor c, int dir, bool has_icons, int count);

// "Back / Cancel" -> "back_cancel": a row label's text key slug.
std::string controls_label_slug(std::string_view label);

}  // namespace sh::screens
