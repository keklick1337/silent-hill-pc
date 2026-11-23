// SPDX-License-Identifier: GPL-3.0-or-later
// A Yes / No question a screen asks before it does something that cannot be undone. It opens on No; Left or Right
// changes the answer, confirm gives it, cancel is No.
#pragma once

namespace sh::menu {

class YesNoPrompt {
public:
    enum class Answer { Pending, Yes, No };

    struct Keys {
        bool left = false;
        bool right = false;
        bool confirm = false;
        bool cancel = false;
    };

    bool is_open() const { return open_; }
    // The answer the highlight is on.
    bool on_yes() const { return on_yes_; }

    void open() {
        open_ = true;
        on_yes_ = false;
    }

    void close() { open_ = false; }

    // The pointer is over an answer.
    void point_at(bool yes) { on_yes_ = yes; }

    // One frame of the open question; the answer closes it.
    Answer update(const Keys& keys) {
        if (!open_) {
            return Answer::Pending;
        }
        if (keys.cancel) {
            open_ = false;
            return Answer::No;
        }
        if (keys.left || keys.right) {
            on_yes_ = !on_yes_;
        }
        if (!keys.confirm) {
            return Answer::Pending;
        }
        open_ = false;
        return on_yes_ ? Answer::Yes : Answer::No;
    }

private:
    bool open_ = false;
    bool on_yes_ = false;
};

}  // namespace sh::menu
