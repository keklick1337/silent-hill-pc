// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Splits a console line into words. Words are separated by spaces and tabs; "double" or 'single' quotes keep
// spaces inside a word (the quotes are not part of its text); a backslash inside quotes escapes the next
// character. An unterminated quote is accepted (the word runs to the end of the line), because completion
// works on half-typed lines. Pure functions, no game state.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sh::console {

struct Token {
    std::string text;        // unquoted, unescaped
    std::size_t begin = 0;   // byte span of the word in the line, quotes included
    std::size_t end = 0;
    bool quoted = false;     // the word was opened with a quote
    bool closed = true;      // false: the opening quote has no partner (half-typed)
};

// All words of `line`, in order.
std::vector<Token> tokenize(std::string_view line);

// Splits a line into separate commands at ';' outside quotes. Empty commands are dropped. The returned views
// point into `line` and are trimmed of surrounding whitespace.
std::vector<std::string_view> split_commands(std::string_view line);

// What the caret is editing: the index of the word it touches (== number of words when the caret is in free
// space after the last word, i.e. a new empty word starts there), the typed prefix of that word (up to the
// caret) and the byte span a completion replaces.
struct Cursor {
    std::size_t index = 0;
    std::string prefix;
    std::size_t replace_begin = 0;
    std::size_t replace_end = 0;
    bool quoted = false;
};

// `caret` is a byte offset 0..line.size(). Only the command being edited counts: a line "a; b c" with the
// caret in "c" reports index 1 for "c" (words are counted from the start of that command), and the
// replace span is in offsets of the whole `line`.
Cursor cursor_at(std::string_view line, std::size_t caret);

// The raw byte span [begin, end) of the ';'-separated command that contains `caret` (end excludes the ';'; not
// trimmed, may be empty). Shared with the completion engine, which needs the whole command being edited.
struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;
};
Span command_span_at(std::string_view line, std::size_t caret);

// Quotes `text` when it needs it (spaces, quotes, ';', empty), for inserting a completion into a line.
std::string quote_if_needed(std::string_view text);

}  // namespace sh::console
