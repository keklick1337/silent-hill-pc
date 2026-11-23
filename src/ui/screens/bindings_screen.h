// SPDX-License-Identifier: GPL-3.0-or-later
// The bindings screen (Options > Controls > Key Bindings): one tab per device (keyboard, mouse, gamepad), each
// listing its actions with a primary and a secondary bind that the player sets by pressing the new input. It is
// drawn with the menus' draw layer (ui/menu/nm_draw.h) and takes its own input straight from the devices
// (input/bind/bind_input.h) and the pointer, so a bad bind cannot lock the player out of fixing it. The binds
// themselves are the bind table's (input/bind/bind_table.h); the layout, colours and texts are
// gamedata/menu/controls_screen.json. Game thread only.
#pragma once

#include <chrono>

#include "engine/menu/yes_no_prompt.h"
#include "input/bind/bind_capture.h"
#include "input/bind/bind_table.h"
#include "input/glyphs/glyph_table.h"
#include "ui/menu/nm_draw.h"
#include "ui/screens/bindings_cursor.h"

namespace sh::ui {

class BindingsScreen {
public:
    // Opens on the binds of the scheme in use (classic, or the alternate cameras'). The options screen shows it
    // between its own fades: it calls update, draw and close itself.
    void open();
    // Opened by a menu row instead: while is_open() the menu calls frame() in place of its own update and draw.
    void open_from_menu();
    // The same, on the Keyboard tab at the row whose first keyboard bind is `bind_key` of the bind table
    // ("action_quick_save").
    void open_at_bind(const char* bind_key);

    // False too once a menu host stopped calling frame() (a warm reset, the menu torn down) or the game left
    // the options screen that showed it.
    bool is_open() const;
    // One frame of input. False on the frame the player leaves; the screen stays drawable until close().
    bool update();
    void draw();
    void close() { open_ = false; }
    // update, then draw or close, and the mouse cursor. False on the frame the player leaves: the menu skips
    // its own frame as well, so the press that closed the screen does not act twice.
    bool frame();

private:
    struct Hit {
        enum class Kind { None, Tab, Icons, Row, Back, Reset, Yes, No };
        Kind kind = Kind::None;
        int index = -1;  // the tab, or the row of the list
        int slot = -1;
    };

    void start(bool hosted);
    void show_tab(BindingsCursor::Tab tab, bool first);
    int bind_row(int index) const;
    sh::input::BindColumn bind_column(int slot) const;
    bool stick_cell(int index, int slot) const;

    void step_tab(int dir);
    void begin_capture();
    void clear_bind();
    void ask_reset();
    bool leave();
    Hit hit(const nm::Input& in) const;
    bool pointer(const nm::Input& in);
    void update_prompt(const sh::input::NavState& pressed, const nm::Input& in);

    void draw_tabs();
    void draw_head();
    void draw_row(int index, int y);
    void draw_cell(int index, int slot, int y, bool selected);
    void draw_stick(int table_row, int slot, int y);
    void draw_footer_item(int x, int y, bool selected, const char* label);

    bool open_ = false;
    bool hosted_ = false;  // shown by a menu row, not by the options screen's state
    std::chrono::steady_clock::time_point last_frame_{};
    BindingsCursor cursor_;
    std::vector<int> rows_;  // bind table rows of the tab, in list order
    bool capturing_ = false;
    sh::menu::YesNoPrompt reset_prompt_;
    sh::input::MenuNav nav_;
    sh::input::GlyphSet icon_set_ = sh::input::GlyphSet::Xbox;
};

BindingsScreen& bindings_screen();

}  // namespace sh::ui
