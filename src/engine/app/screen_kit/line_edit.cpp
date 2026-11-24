// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/screen_kit/line_edit.h"

#include <algorithm>

namespace sh::screen_kit {

namespace {

bool continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

}  // namespace

void LineEdit::set(std::string text) {
    text_.clear();
    caret_ = 0;
    insert(text);
}

void LineEdit::insert(std::string_view utf8) {
    std::string clean;
    clean.reserve(utf8.size());
    for (char c : utf8) {
        const auto u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u != 0x7F) clean += c;
    }
    text_.insert(caret_, clean);
    caret_ += clean.size();
}

std::size_t LineEdit::previous(std::size_t at) const {
    if (at == 0) return 0;
    --at;
    while (at > 0 && continuation(text_[at])) --at;
    return at;
}

std::size_t LineEdit::next(std::size_t at) const {
    if (at >= text_.size()) return text_.size();
    ++at;
    while (at < text_.size() && continuation(text_[at])) ++at;
    return at;
}

void LineEdit::backspace() {
    const std::size_t from = previous(caret_);
    text_.erase(from, caret_ - from);
    caret_ = from;
}

void LineEdit::erase_forward() { text_.erase(caret_, next(caret_) - caret_); }

void LineEdit::left() { caret_ = previous(caret_); }

void LineEdit::right() { caret_ = next(caret_); }

void LineEdit::place(std::size_t byte) {
    byte = std::min(byte, text_.size());
    while (byte > 0 && byte < text_.size() && continuation(text_[byte])) --byte;
    caret_ = byte;
}

}  // namespace sh::screen_kit
