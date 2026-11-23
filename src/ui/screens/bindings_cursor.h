// SPDX-License-Identifier: GPL-3.0-or-later
// Where the player is on the bindings screen (Options > Controls > Key Bindings): the device tab, the part of the
// screen that has the focus, the row of the list and the bind slot in it. Rows differ in the slots they have (an
// action may have no secondary bind, a movement row shows the stick instead of a pad bind), so the cursor keeps
// the slot the player asked for and settles on the nearest one a row has.
#pragma once

#include <array>
#include <utility>
#include <vector>

#include "engine/menu/scroll_window.h"
#include "engine/screens/controls_layout.h"

namespace sh::ui {

class BindingsCursor {
public:
    using Tab = sh::screens::ControlsTab;
    using Focus = sh::screens::ControlsFocus;
    // The slots a row of the list has on the tab's device.
    using RowSlots = std::array<bool, sh::screens::kControlsSlots>;

    Tab tab() const { return tab_; }
    Focus focus() const { return focus_; }
    int slot() const { return slot_; }
    int row() const { return list_.selected(); }
    int row_count() const { return static_cast<int>(rows_.size()); }
    const sh::menu::ScrollWindow& list() const { return list_; }
    bool on_gamepad_tab() const { return tab_ == Tab::Gamepad; }
    bool on_row(int index) const { return focus_ == Focus::List && index == list_.selected(); }

    bool has_slot(int index, int slot) const {
        return index >= 0 && index < row_count() && slot >= 0 && slot < sh::screens::kControlsSlots &&
               rows_[static_cast<std::size_t>(index)][static_cast<std::size_t>(slot)];
    }

    // The screen opens on the list, on the tab it was left on.
    void reset(Tab tab, std::vector<RowSlots> rows, int visible_rows) {
        focus_ = Focus::List;
        slot_ = 0;
        wanted_slot_ = 0;
        show_tab(tab, std::move(rows), visible_rows);
    }

    // Another tab's list: it starts from its top, and a focus the new tab has no place for moves to the tabs.
    void show_tab(Tab tab, std::vector<RowSlots> rows, int visible_rows) {
        tab_ = tab;
        rows_ = std::move(rows);
        list_ = sh::menu::ScrollWindow(row_count(), visible_rows);
        if (focus_ == Focus::Icons && !on_gamepad_tab()) {
            focus_ = Focus::Tabs;
        }
        if (focus_ == Focus::List) {
            if (rows_.empty()) {
                focus_ = Focus::Tabs;
            } else {
                focus_row(0);
            }
        }
    }

    void focus_on(Focus focus) { focus_ = focus; }

    void focus_row(int index) {
        focus_ = Focus::List;
        list_.select(index);
        settle_slot();
    }

    // The same, on the slot the pointer is over when the row has it.
    void focus_cell(int index, int slot) {
        if (has_slot(index, slot)) {
            wanted_slot_ = slot;
        }
        focus_row(index);
    }

    // Up or down through the tabs, the Gamepad Icons row, the list and the footer.
    void move_vertical(int dir) {
        sh::screens::ControlsCursor c;
        c.focus = focus_;
        c.row = list_.selected();
        c = sh::screens::controls_move_vertical(c, dir, on_gamepad_tab(), row_count());
        if (c.focus == Focus::List) {
            focus_row(c.row);
        } else {
            focus_ = c.focus;
        }
    }

    // Left or right between the slots of the row. True when the slot changed.
    bool move_slot(int dir) {
        const int slots = sh::screens::kControlsSlots;
        const int other = (slot_ + (dir > 0 ? 1 : slots - 1)) % slots;
        if (other == slot_ || !has_slot(list_.selected(), other)) {
            return false;
        }
        slot_ = other;
        wanted_slot_ = other;
        return true;
    }

    // Left or right between Back and Reset.
    void swap_footer() { focus_ = focus_ == Focus::Back ? Focus::Reset : Focus::Back; }

    void wheel(int notches) {
        list_.wheel(notches);
        if (focus_ == Focus::List) {
            settle_slot();
        }
    }

private:
    void settle_slot() {
        const int row = list_.selected();
        const int other = 1 - wanted_slot_;
        slot_ = has_slot(row, wanted_slot_) ? wanted_slot_ : has_slot(row, other) ? other : wanted_slot_;
    }

    Tab tab_ = Tab::Keyboard;
    Focus focus_ = Focus::List;
    std::vector<RowSlots> rows_;
    sh::menu::ScrollWindow list_;
    int slot_ = 0;
    int wanted_slot_ = 0;
};

}  // namespace sh::ui
