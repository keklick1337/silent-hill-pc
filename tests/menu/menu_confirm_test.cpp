// SPDX-License-Identifier: GPL-3.0-or-later
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
// Action rows with `confirm`: the menu asks Yes / No before the action runs (the pause menu's EXIT).

#include "engine/menu/menu_model.h"
#include "framework/sh_test.h"

using namespace sh::menu;

namespace {

struct Ask {
    Registry reg;
    int fired = 0;

    explicit Ask(bool close = false) {
        reg.native_action("ask", [this] { ++fired; });
        MenuDef m;
        m.id = "ask_menu";
        m.title = "t";
        m.rows.push_back(RowDef{.kind = RowKind::Action, .id = "ask", .label = "Ask", .native = "ask",
                                .close_on_activate = close, .confirm = "ui.q"});
        reg.define(m);
    }
};

MenuInput key_confirm() { MenuInput i; i.confirm = true; return i; }
MenuInput key_cancel() { MenuInput i; i.cancel = true; return i; }
MenuInput key_left() { MenuInput i; i.left = true; return i; }
MenuInput key_right() { MenuInput i; i.right = true; return i; }

}  // namespace

SH_TEST(menu_confirm_row_asks_before_running) {
    Ask f;
    Controller c(f.reg);
    c.open("ask_menu");

    // Confirm opens the question; the default answer is No and nothing ran.
    SH_CHECK(c.update(key_confirm()) == Event::Entered);
    SH_CHECK(c.confirm_open() && !c.confirm_yes() && c.confirm_question() == "ui.q" && f.fired == 0);
    SH_CHECK(c.update(key_confirm()) == Event::Back);
    SH_CHECK(!c.confirm_open() && f.fired == 0 && c.is_open());

    // Cancel is No too, and does not close the menu.
    c.update(key_confirm());
    SH_CHECK(c.update(key_cancel()) == Event::Back);
    SH_CHECK(!c.confirm_open() && c.is_open() && f.fired == 0);

    // Left picks Yes, right goes back to No; Yes runs the action.
    c.update(key_confirm());
    SH_CHECK(c.update(key_left()) == Event::Moved && c.confirm_yes());
    SH_CHECK(c.update(key_right()) == Event::Moved && !c.confirm_yes());
    c.update(key_left());
    SH_CHECK(c.update(key_confirm()) == Event::Activated);
    SH_CHECK(f.fired == 1 && !c.confirm_open());
}

SH_TEST(menu_confirm_pointer_close_and_disabled) {
    Ask f(true);
    Controller c(f.reg);
    c.open("ask_menu");
    c.update(key_confirm());

    // Hovering Yes selects it; a click on it answers and runs the action, closing the menu.
    MenuInput hover;
    hover.hover_answer = 0;
    hover.pointer_moved = true;
    SH_CHECK(c.update(hover) == Event::Moved && c.confirm_yes());
    MenuInput click;
    click.hover_answer = 0;
    click.click = true;
    SH_CHECK(c.update(click) == Event::Closed);
    SH_CHECK(f.fired == 1 && !c.is_open());

    // A disabled row refuses before asking.
    f.reg.native_enabled("ask", [] { return false; });
    c.open("ask_menu");
    SH_CHECK(c.update(key_confirm()) == Event::Refused && !c.confirm_open());
}
