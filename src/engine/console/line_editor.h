// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The one-line text editor behind the prompt: UTF-8 text with a caret at a byte offset that always sits on a
// character boundary. No SDL: the console feeds it decoded events.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sh::console {

class LineEditor {
public:
    const std::string& text() const { return text_; }
    std::size_t caret() const { return caret_; }
    bool empty() const { return text_.empty(); }

    void set(std::string_view text);                     // replaces the text, caret at the end
    void set(std::string_view text, std::size_t caret);  // caret clamped and moved to a boundary
    void clear();

    // Inserts printable text at the caret. Control characters (< 0x20, 0x7F) are dropped; a newline in pasted
    // text ends the insertion (a prompt is one line). Returns the number of bytes inserted.
    std::size_t insert(std::string_view utf8);

    void backspace();                // delete the character before the caret
    void del();                      // delete the character at the caret
    void left();
    void right();
    void home();
    void end();
    void word_left();                // to the start of the previous word
    void word_right();               // to the end of the next word
    void kill_word_back();           // Ctrl+W
    void kill_to_start();            // Ctrl+U
    void kill_to_end();              // Ctrl+K

    static std::size_t next_boundary(std::string_view s, std::size_t pos);  // one character right
    static std::size_t prev_boundary(std::string_view s, std::size_t pos);  // one character left

private:
    std::string text_;
    std::size_t caret_ = 0;
};

}  // namespace sh::console
