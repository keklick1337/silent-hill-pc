// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/console/line_editor.h"

namespace sh::console {

namespace {

bool is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

// Non-ASCII bytes count as word characters so that accented and CJK text is not split into "separators".
bool is_word_byte(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

}  // namespace

std::size_t LineEditor::next_boundary(std::string_view s, std::size_t pos) {
    if (pos >= s.size()) return s.size();
    ++pos;
    while (pos < s.size() && is_continuation(static_cast<unsigned char>(s[pos]))) ++pos;
    return pos;
}

std::size_t LineEditor::prev_boundary(std::string_view s, std::size_t pos) {
    if (pos > s.size()) pos = s.size();
    if (pos == 0) return 0;
    --pos;
    while (pos > 0 && is_continuation(static_cast<unsigned char>(s[pos]))) --pos;
    return pos;
}

void LineEditor::set(std::string_view text) {
    text_.assign(text);
    caret_ = text_.size();
}

void LineEditor::set(std::string_view text, std::size_t caret) {
    text_.assign(text);
    if (caret > text_.size()) caret = text_.size();
    while (caret > 0 && caret < text_.size() && is_continuation(static_cast<unsigned char>(text_[caret]))) --caret;
    caret_ = caret;
}

void LineEditor::clear() {
    text_.clear();
    caret_ = 0;
}

std::size_t LineEditor::insert(std::string_view utf8) {
    std::string filtered;
    filtered.reserve(utf8.size());
    for (const char ch : utf8) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '\n' || c == '\r') break;
        if (c < 0x20 || c == 0x7F) continue;
        filtered.push_back(ch);
    }
    text_.insert(caret_, filtered);
    caret_ += filtered.size();
    return filtered.size();
}

void LineEditor::backspace() {
    if (caret_ == 0) return;
    const std::size_t from = prev_boundary(text_, caret_);
    text_.erase(from, caret_ - from);
    caret_ = from;
}

void LineEditor::del() {
    if (caret_ >= text_.size()) return;
    text_.erase(caret_, next_boundary(text_, caret_) - caret_);
}

void LineEditor::left() { caret_ = prev_boundary(text_, caret_); }
void LineEditor::right() { caret_ = next_boundary(text_, caret_); }
void LineEditor::home() { caret_ = 0; }
void LineEditor::end() { caret_ = text_.size(); }

void LineEditor::word_left() {
    std::size_t pos = caret_;
    while (pos > 0 && !is_word_byte(static_cast<unsigned char>(text_[pos - 1]))) --pos;
    while (pos > 0 && is_word_byte(static_cast<unsigned char>(text_[pos - 1]))) --pos;
    caret_ = pos;
}

void LineEditor::word_right() {
    std::size_t pos = caret_;
    while (pos < text_.size() && !is_word_byte(static_cast<unsigned char>(text_[pos]))) ++pos;
    while (pos < text_.size() && is_word_byte(static_cast<unsigned char>(text_[pos]))) ++pos;
    caret_ = pos;
}

void LineEditor::kill_word_back() {
    const std::size_t to = caret_;
    word_left();
    text_.erase(caret_, to - caret_);
}

void LineEditor::kill_to_start() {
    text_.erase(0, caret_);
    caret_ = 0;
}

void LineEditor::kill_to_end() { text_.erase(caret_); }

}  // namespace sh::console
