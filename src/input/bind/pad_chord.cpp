// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "input/bind/pad_chord.h"

#include <algorithm>
#include <cctype>

namespace sh::input {
namespace {

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

bool is_none(std::string_view s) {
    return s.empty() || (s.size() == 4 && std::toupper(static_cast<unsigned char>(s[0])) == 'N' &&
                         std::toupper(static_cast<unsigned char>(s[1])) == 'O' &&
                         std::toupper(static_cast<unsigned char>(s[2])) == 'N' &&
                         std::toupper(static_cast<unsigned char>(s[3])) == 'E');
}

}  // namespace

std::vector<std::string> chord_parts(std::string_view value) {
    std::vector<std::string> out;
    while (out.size() < kChordMaxButtons) {
        const std::size_t cut = value.find(kChordSeparator);
        const std::string_view part = trimmed(value.substr(0, cut));
        if (!is_none(part)) out.emplace_back(part);
        if (cut == std::string_view::npos) break;
        value.remove_prefix(cut + 1);
    }
    return out;
}

std::string chord_value(std::span<const std::string> parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size() && i < kChordMaxButtons; ++i) {
        if (!out.empty()) out += kChordSeparator;
        out += parts[i];
    }
    return out.empty() ? std::string("NONE") : out;
}

bool is_chord(std::string_view value) { return chord_parts(value).size() == kChordMaxButtons; }

PadBind parse_pad_bind(std::string_view value, const std::function<int(const std::string&)>& lookup) {
    PadBind bind;
    const std::vector<std::string> parts = chord_parts(value);
    for (const std::string& part : parts) {
        const int id = lookup(part);
        if (id < 0 || (bind.count == 1 && bind.buttons[0] == id)) return {};
        bind.buttons[static_cast<std::size_t>(bind.count++)] = id;
    }
    return bind;
}

void ChordResolver::set_binds(std::vector<PadBind> binds) {
    binds_ = std::move(binds);
    state_.assign(binds_.size(), State{});
    buttons_.clear();
    for (const PadBind& b : binds_) {
        for (int i = 0; i < b.count; ++i) {
            if (button(b.buttons[static_cast<std::size_t>(i)]) == nullptr) {
                buttons_.push_back(Button{b.buttons[static_cast<std::size_t>(i)]});
            }
        }
    }
}

void ChordResolver::reset() {
    state_.assign(binds_.size(), State{});
    for (Button& b : buttons_) b = Button{b.id};
}

ChordResolver::Button* ChordResolver::button(int id) {
    auto it = std::find_if(buttons_.begin(), buttons_.end(), [id](const Button& b) { return b.id == id; });
    return it != buttons_.end() ? &*it : nullptr;
}

const ChordResolver::Button* ChordResolver::button(int id) const {
    return const_cast<ChordResolver*>(this)->button(id);
}

bool ChordResolver::chord_active(int id) const {
    for (std::size_t i = 0; i < binds_.size(); ++i) {
        const PadBind& b = binds_[i];
        if (b.chord() && state_[i].active && (b.buttons[0] == id || b.buttons[1] == id)) return true;
    }
    return false;
}

bool ChordResolver::chord_possible(int id, std::uint32_t now_ms) const {
    for (const PadBind& b : binds_) {
        if (!b.chord() || (b.buttons[0] != id && b.buttons[1] != id)) continue;
        const Button* other = button(b.buttons[0] == id ? b.buttons[1] : b.buttons[0]);
        // Held from before the window, the other button can no longer make the combination with this press.
        if (!other->down || now_ms - other->press_ms <= kChordWindowMs) return true;
    }
    return false;
}

void ChordResolver::step(std::uint32_t now_ms, const std::function<bool(int)>& held) {
    for (Button& b : buttons_) {
        const bool down = held(b.id);
        b.pressed = down && !b.down;
        if (b.pressed) b.press_ms = now_ms;
        b.down = down;
    }

    // The combinations first, so a button they take this frame does not also fire alone.
    for (std::size_t i = 0; i < binds_.size(); ++i) {
        State& s = state_[i];
        s.clicked = false;
        const PadBind& bind = binds_[i];
        if (!bind.chord()) continue;
        const Button* a = button(bind.buttons[0]);
        const Button* b = button(bind.buttons[1]);
        const bool both = a->down && b->down;
        if (!both) {
            s.active = false;
            continue;
        }
        if (s.active || !(a->pressed || b->pressed)) continue;
        const std::uint32_t apart = a->press_ms > b->press_ms ? a->press_ms - b->press_ms : b->press_ms - a->press_ms;
        if (apart <= kChordWindowMs) {
            s.clicked = true;
            s.active = true;
        }
    }

    for (std::size_t i = 0; i < binds_.size(); ++i) {
        State& s = state_[i];
        const PadBind& bind = binds_[i];
        if (bind.count != 1) continue;
        const int id = bind.buttons[0];
        const Button* b = button(id);
        if (!b->down && !s.pending) s.taken = false;
        if (b->pressed && !s.pending) {
            if (chord_active(id)) {
                s.taken = true;
            } else if (chord_possible(id, now_ms)) {
                s.pending = true;
                s.since_ms = now_ms;
            } else {
                s.clicked = true;
                s.taken = false;
            }
        }
        if (!s.pending) continue;
        if (chord_active(id)) {
            s.pending = false;
            s.taken = b->down;
        } else if (now_ms - s.since_ms >= kChordWindowMs) {
            // No combination came: the press was this button's own, a tap too.
            s.pending = false;
            s.clicked = true;
        }
    }
}

bool ChordResolver::clicked(std::size_t bind) const { return bind < state_.size() && state_[bind].clicked; }

bool ChordResolver::held(std::size_t bind) const {
    if (bind >= binds_.size()) return false;
    const PadBind& b = binds_[bind];
    const State& s = state_[bind];
    if (b.chord()) return s.active;
    if (b.count != 1) return false;
    const Button* button_state = button(b.buttons[0]);
    return button_state->down && !s.pending && !s.taken && !chord_active(b.buttons[0]);
}

}  // namespace sh::input
