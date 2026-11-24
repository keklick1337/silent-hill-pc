// SPDX-License-Identifier: GPL-3.0-or-later
// A one-line text field's text and caret, in UTF-8: the caret is a byte offset that always sits between two
// characters. No drawing and no SDL: the screen forwards the keys and the typed text.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sh::screen_kit {

class LineEdit {
public:
    const std::string& text() const { return text_; }
    std::size_t caret() const { return caret_; }

    // Replaces the text; the caret goes to its end.
    void set(std::string text);
    // Typed or pasted text at the caret. Line breaks, tabs and other control characters are dropped (a pasted path
    // often ends with a line break).
    void insert(std::string_view utf8);
    void backspace();
    void erase_forward();
    void left();
    void right();
    void home() { caret_ = 0; }
    void end() { caret_ = text_.size(); }
    // The caret at the character boundary at or before `byte`.
    void place(std::size_t byte);

private:
    std::size_t previous(std::size_t at) const;
    std::size_t next(std::size_t at) const;

    std::string text_;
    std::size_t caret_ = 0;
};

}  // namespace sh::screen_kit
