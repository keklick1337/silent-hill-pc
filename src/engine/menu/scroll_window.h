// SPDX-License-Identifier: GPL-3.0-or-later
// A list longer than the screen: `visible` rows of `count` are shown from `top`, and the selected row is always one
// of them. The keys move the selection and the window follows it; the wheel moves the window and the selection
// follows that.
#pragma once

#include <algorithm>

namespace sh::menu {

class ScrollWindow {
public:
    ScrollWindow() = default;
    ScrollWindow(int count, int visible) : count_(std::max(count, 0)), visible_(std::max(visible, 1)) {}

    int count() const { return count_; }
    int visible() const { return visible_; }
    int top() const { return top_; }
    int selected() const { return selected_; }
    // One past the last row on screen.
    int bottom() const { return std::min(top_ + visible_, count_); }

    bool can_scroll_up() const { return top_ > 0; }
    bool can_scroll_down() const { return top_ + visible_ < count_; }

    // The list grew or shrank; the window and the selection stay where they still fit.
    void set_count(int count) {
        count_ = std::max(count, 0);
        settle();
    }

    // Moves the selection by `delta` rows, past the ends to the other end when `wrap`. True when it moved.
    bool move(int delta, bool wrap) {
        if (count_ == 0) {
            return false;
        }
        const int before = selected_;
        int target = selected_ + delta;
        if (wrap) {
            target %= count_;
            if (target < 0) {
                target += count_;
            }
        }
        select(target);
        return selected_ != before;
    }

    void select(int index) {
        selected_ = index;
        settle();
        reveal_selected();
    }

    // Wheel notches, positive towards the top of the list.
    void wheel(int notches) {
        top_ -= notches;
        settle();
        selected_ = std::clamp(selected_, top_, top_ + visible_ - 1);
        settle();
    }

private:
    void settle() {
        top_ = std::clamp(top_, 0, std::max(count_ - visible_, 0));
        selected_ = std::clamp(selected_, 0, std::max(count_ - 1, 0));
    }

    void reveal_selected() {
        if (selected_ < top_) {
            top_ = selected_;
        } else if (selected_ >= top_ + visible_) {
            top_ = selected_ - visible_ + 1;
        }
        settle();
    }

    int count_ = 0;
    int visible_ = 1;
    int top_ = 0;
    int selected_ = 0;
};

}  // namespace sh::menu
