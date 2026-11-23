// SPDX-License-Identifier: GPL-3.0-or-later
// The cursor of the bindings screen (src/ui/screens/bindings_cursor.h): tabs, focus, the row of the list and
// the bind slot a row may or may not have.

#include "ui/screens/bindings_cursor.h"

#include "framework/sh_test.h"

#include <vector>

namespace {

using sh::ui::BindingsCursor;
using Focus = BindingsCursor::Focus;
using Tab = BindingsCursor::Tab;
using Slots = BindingsCursor::RowSlots;

constexpr Slots kBoth{true, true};
constexpr Slots kPrimary{true, false};
constexpr Slots kSecondary{false, true};
constexpr Slots kNone{false, false};  // a movement row on the Gamepad tab: the stick is shown

// Twelve rows: both slots, except row 2 (primary only), row 3 (secondary only) and row 4 (none).
std::vector<Slots> rows() {
    std::vector<Slots> out(12, kBoth);
    out[2] = kPrimary;
    out[3] = kSecondary;
    out[4] = kNone;
    return out;
}

BindingsCursor opened(Tab tab = Tab::Keyboard) {
    BindingsCursor c;
    c.reset(tab, rows(), 8);
    return c;
}

}  // namespace

SH_TEST(bindings_cursor_opens_on_the_list) {
    const BindingsCursor c = opened();
    SH_CHECK(c.tab() == Tab::Keyboard && c.focus() == Focus::List && c.row() == 0 && c.slot() == 0);
    SH_CHECK(c.row_count() == 12 && c.list().visible() == 8 && c.list().top() == 0);
    SH_CHECK(c.on_row(0) && !c.on_row(1) && !c.on_gamepad_tab());
    SH_CHECK(c.has_slot(0, 1) && !c.has_slot(2, 1) && !c.has_slot(4, 0) && !c.has_slot(12, 0) && !c.has_slot(0, 2) &&
             !c.has_slot(-1, 0) && !c.has_slot(0, -1));

    BindingsCursor empty;
    empty.reset(Tab::Mouse, {}, 8);  // a tab without rows has nothing to stand on in the list
    SH_CHECK(empty.focus() == Focus::Tabs && empty.row_count() == 0);
}

SH_TEST(bindings_cursor_slot_follows_what_a_row_has) {
    BindingsCursor c = opened();
    SH_CHECK(c.move_slot(1) && c.slot() == 1);  // the player wants the secondary slot from here on
    SH_CHECK(opened().move_slot(-1));  // left from the primary wraps to the secondary
    c.move_vertical(1);
    SH_CHECK(c.row() == 1 && c.slot() == 1);
    c.move_vertical(1);
    SH_CHECK(c.row() == 2 && c.slot() == 0);  // no secondary on this row: the primary stands in
    SH_CHECK(!c.move_slot(1) && c.slot() == 0);
    c.move_vertical(1);
    SH_CHECK(c.row() == 3 && c.slot() == 1);  // and the wish is remembered
    SH_CHECK(!c.move_slot(-1) && c.slot() == 1);
    c.move_vertical(1);
    SH_CHECK(c.row() == 4 && c.slot() == 1);  // neither: the wish itself
    c.move_vertical(1);
    SH_CHECK(c.row() == 5 && c.slot() == 1);
    SH_CHECK(c.move_slot(1) && c.slot() == 0);
    c.focus_row(3);
    SH_CHECK(c.slot() == 1);  // the primary is wanted now, the row has only the secondary
    c.focus_row(0);
    SH_CHECK(c.slot() == 0);
}

SH_TEST(bindings_cursor_moves_through_the_screen) {
    BindingsCursor c = opened();
    c.move_vertical(-1);
    SH_CHECK(c.focus() == Focus::Tabs);  // no Gamepad Icons row on this tab
    c.move_vertical(-1);
    SH_CHECK(c.focus() == Focus::Back);
    c.swap_footer();
    SH_CHECK(c.focus() == Focus::Reset);
    c.swap_footer();
    SH_CHECK(c.focus() == Focus::Back);
    c.swap_footer();
    c.move_vertical(-1);
    SH_CHECK(c.focus() == Focus::List && c.row() == 11 && c.list().top() == 4);  // the list scrolled to its end
    c.move_vertical(1);
    SH_CHECK(c.focus() == Focus::Back);
    c.move_vertical(1);
    SH_CHECK(c.focus() == Focus::Tabs);
    c.move_vertical(1);
    SH_CHECK(c.focus() == Focus::List && c.row() == 0 && c.list().top() == 0);
    SH_CHECK(!c.on_row(1) && c.on_row(0));

    BindingsCursor pad = opened(Tab::Gamepad);
    SH_CHECK(pad.on_gamepad_tab());
    pad.move_vertical(-1);
    SH_CHECK(pad.focus() == Focus::Icons);
    pad.move_vertical(-1);
    SH_CHECK(pad.focus() == Focus::Tabs);
    pad.move_vertical(1);
    SH_CHECK(pad.focus() == Focus::Icons && !pad.on_row(0));
}

SH_TEST(bindings_cursor_changes_tabs) {
    BindingsCursor c = opened(Tab::Gamepad);
    c.focus_row(9);
    SH_CHECK(c.list().top() == 2);
    c.show_tab(Tab::Keyboard, rows(), 8);  // from the list: the new tab's first row
    SH_CHECK(c.tab() == Tab::Keyboard && c.focus() == Focus::List && c.row() == 0 && c.list().top() == 0);

    c.show_tab(Tab::Gamepad, rows(), 8);
    c.focus_on(Focus::Icons);
    c.show_tab(Tab::Gamepad, rows(), 8);
    SH_CHECK(c.focus() == Focus::Icons);
    c.show_tab(Tab::Mouse, rows(), 8);  // the Icons row is the Gamepad tab's alone
    SH_CHECK(c.focus() == Focus::Tabs);
    c.show_tab(Tab::Keyboard, rows(), 8);
    SH_CHECK(c.focus() == Focus::Tabs && c.row() == 0);  // stepping tabs from the tabs stays on them

    c.focus_on(Focus::Reset);
    c.show_tab(Tab::Mouse, {}, 8);
    SH_CHECK(c.focus() == Focus::Reset && c.row_count() == 0);
    c.move_vertical(-1);
    SH_CHECK(c.focus() == Focus::Tabs);  // nothing in the list to stop on
    c.focus_on(Focus::List);
    c.show_tab(Tab::Mouse, {}, 8);
    SH_CHECK(c.focus() == Focus::Tabs);

    // The wanted slot outlives a change of tab; the screen opening again forgets it.
    BindingsCursor wish = opened();
    wish.move_slot(1);
    wish.show_tab(Tab::Mouse, rows(), 8);
    SH_CHECK(wish.slot() == 1);
    wish.reset(Tab::Mouse, rows(), 8);
    SH_CHECK(wish.slot() == 0 && wish.tab() == Tab::Mouse && wish.focus() == Focus::List);
}

SH_TEST(bindings_cursor_pointer_and_wheel) {
    BindingsCursor c = opened();
    c.focus_cell(6, 1);  // the pointer over a cell: that row, that slot
    SH_CHECK(c.focus() == Focus::List && c.row() == 6 && c.slot() == 1);
    c.focus_cell(2, 1);  // over a slot the row lacks: the row, the nearest slot
    SH_CHECK(c.row() == 2 && c.slot() == 0);
    c.focus_cell(7, -1);  // over the label: the slot the player last asked for
    SH_CHECK(c.row() == 7 && c.slot() == 1);
    c.focus_cell(5, 0);
    SH_CHECK(c.row() == 5 && c.slot() == 0);

    c.focus_on(Focus::Tabs);
    c.wheel(-3);  // the list scrolls under another focus
    SH_CHECK(c.focus() == Focus::Tabs && c.list().top() == 3 && c.row() == 5);
    c.focus_row(3);
    SH_CHECK(c.list().top() == 3 && c.slot() == 1);  // row 3 has only the secondary
    c.wheel(-1);
    SH_CHECK(c.list().top() == 4 && c.row() == 4 && c.slot() == 0);  // pushed onto row 4, which has neither
    c.wheel(-1);
    SH_CHECK(c.list().top() == 4 && c.row() == 4);  // the list's end
    c.wheel(9);
    SH_CHECK(c.list().top() == 0 && c.row() == 4);
}
