// SPDX-License-Identifier: GPL-3.0-or-later
// The scrolling list window (src/engine/menu/scroll_window.h) and the Yes / No prompt
// (src/engine/menu/yes_no_prompt.h).

#include "engine/menu/scroll_window.h"
#include "engine/menu/yes_no_prompt.h"
#include "framework/sh_test.h"

using sh::menu::ScrollWindow;
using sh::menu::YesNoPrompt;

SH_TEST(scroll_window_starts_at_the_top) {
    const ScrollWindow w(10, 4);
    SH_CHECK(w.count() == 10 && w.visible() == 4 && w.top() == 0 && w.selected() == 0 && w.bottom() == 4);
    SH_CHECK(!w.can_scroll_up() && w.can_scroll_down());

    const ScrollWindow odd(-3, 0);  // a list is never negative, a window never empty
    SH_CHECK(odd.count() == 0 && odd.visible() == 1 && odd.bottom() == 0);
    SH_CHECK(!odd.can_scroll_up() && !odd.can_scroll_down());

    const ScrollWindow unset;
    SH_CHECK(unset.count() == 0 && unset.visible() == 1 && unset.selected() == 0);
}

SH_TEST(scroll_window_follows_the_selection) {
    ScrollWindow w(10, 4);
    SH_CHECK(w.move(1, false) && w.selected() == 1 && w.top() == 0);
    w.move(1, false);
    w.move(1, false);
    SH_CHECK(w.selected() == 3 && w.top() == 0);  // the last row on screen
    SH_CHECK(w.move(1, false) && w.selected() == 4 && w.top() == 1);
    w.select(9);
    SH_CHECK(w.selected() == 9 && w.top() == 6 && w.bottom() == 10 && w.can_scroll_up() && !w.can_scroll_down());
    SH_CHECK(!w.move(1, false) && w.selected() == 9);  // the end stops
    SH_CHECK(w.move(1, true) && w.selected() == 0 && w.top() == 0);  // or wraps
    SH_CHECK(w.move(-1, true) && w.selected() == 9 && w.top() == 6);
    SH_CHECK(!ScrollWindow(10, 4).move(-1, false));
    w.select(7);
    SH_CHECK(w.selected() == 7 && w.top() == 6);  // already on screen: the window stays
    w.select(2);
    SH_CHECK(w.selected() == 2 && w.top() == 2);
    w.select(-5);
    SH_CHECK(w.selected() == 0 && w.top() == 0);
    w.select(99);
    SH_CHECK(w.selected() == 9 && w.top() == 6);
    SH_CHECK(w.move(-25, true) && w.selected() == 4);  // 9 - 25 = -16, wrapped into 0..9
}

SH_TEST(scroll_window_wheel_moves_the_window) {
    ScrollWindow w(10, 4);
    w.wheel(-1);  // towards the end
    SH_CHECK(w.top() == 1 && w.selected() == 1);  // the selection is pushed back onto the screen
    w.wheel(-10);
    SH_CHECK(w.top() == 6 && w.selected() == 6);
    w.select(9);
    w.wheel(2);
    SH_CHECK(w.top() == 4 && w.selected() == 7);  // the last row on screen
    w.wheel(1);
    SH_CHECK(w.top() == 3 && w.selected() == 6);
    w.wheel(50);
    SH_CHECK(w.top() == 0 && w.selected() == 3);

    ScrollWindow small(2, 4);  // nothing to scroll
    small.select(1);
    small.wheel(-3);
    SH_CHECK(small.top() == 0 && small.selected() == 1);
}

SH_TEST(scroll_window_keeps_its_place_when_the_list_changes) {
    ScrollWindow w(10, 4);
    w.select(9);
    w.set_count(8);
    SH_CHECK(w.count() == 8 && w.selected() == 7 && w.top() == 4);
    w.set_count(20);
    SH_CHECK(w.selected() == 7 && w.top() == 4 && w.can_scroll_down());
    w.set_count(3);
    SH_CHECK(w.selected() == 2 && w.top() == 0 && !w.can_scroll_down());
    w.set_count(0);
    SH_CHECK(w.selected() == 0 && w.top() == 0 && !w.move(1, true) && !w.move(1, false));
    w.set_count(-1);
    SH_CHECK(w.count() == 0);
}

SH_TEST(yes_no_prompt_answers) {
    YesNoPrompt p;
    YesNoPrompt::Keys none, left, right, confirm, cancel;
    left.left = true;
    right.right = true;
    confirm.confirm = true;
    cancel.cancel = true;

    SH_CHECK(!p.is_open() && p.update(confirm) == YesNoPrompt::Answer::Pending);  // closed: nothing to answer

    p.open();
    SH_CHECK(p.is_open() && !p.on_yes());  // it opens on No
    SH_CHECK(p.update(none) == YesNoPrompt::Answer::Pending && p.is_open());
    SH_CHECK(p.update(confirm) == YesNoPrompt::Answer::No && !p.is_open());

    p.open();
    SH_CHECK(p.update(left) == YesNoPrompt::Answer::Pending && p.on_yes());
    SH_CHECK(p.update(right) == YesNoPrompt::Answer::Pending && !p.on_yes());  // either key changes the answer
    SH_CHECK(p.update(right) == YesNoPrompt::Answer::Pending && p.on_yes());
    SH_CHECK(p.update(confirm) == YesNoPrompt::Answer::Yes && !p.is_open());

    p.open();
    SH_CHECK(!p.on_yes());  // the last answer is not remembered
    p.point_at(true);
    YesNoPrompt::Keys both = cancel;
    both.confirm = true;
    SH_CHECK(p.update(both) == YesNoPrompt::Answer::No && !p.is_open());  // cancel wins, whatever is chosen

    p.open();
    YesNoPrompt::Keys move_and_confirm = left;
    move_and_confirm.confirm = true;
    SH_CHECK(p.update(move_and_confirm) == YesNoPrompt::Answer::Yes);  // the move of the frame counts first

    p.open();
    p.close();
    SH_CHECK(!p.is_open());
}
