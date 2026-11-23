// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/commands/scripted_run.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace sh::debug {

namespace {

// Bit order of the pad word.
constexpr std::string_view kButtons[16] = {
    "Select", "L3", "R3", "Start", "Up", "Right", "Down", "Left",
    "L2", "R2", "L1", "R1", "Triangle", "Circle", "Cross", "Square",
};
constexpr std::size_t kLongestName = 31;

bool blank(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

Result<std::uint16_t> parse_pad_buttons(std::string_view text) {
    std::uint16_t held = 0;
    std::size_t at = 0;
    for (;;) {
        while (at < text.size() && blank(text[at])) ++at;
        // A name starts here or the list is over (a '+' with no name before it ends it too).
        std::size_t end = at;
        while (end < text.size() && end - at < kLongestName && text[end] != ' ' && text[end] != '+' &&
               text[end] != '\t') {
            ++end;
        }
        if (end == at) break;
        const std::string_view name = text.substr(at, end - at);
        at = end;
        while (at < text.size() && text[at] == '+') ++at;
        if (name == "-") continue;

        bool known = false;
        for (std::size_t bit = 0; bit < 16; ++bit) {
            if (same_name(name, kButtons[bit])) {
                held = static_cast<std::uint16_t>(held | (1u << bit));
                known = true;
            }
        }
        if (!known) return Result<std::uint16_t>::fail(std::string(name));
    }
    return Result<std::uint16_t>::ok(held);
}

void StallDice::configure(int percent, std::uint32_t seed) {
    percent_ = std::clamp(percent, 0, 100);
    state_ = seed;
}

std::uint32_t StallDice::next() {
    state_ = state_ * 1664525u + 1013904223u;
    return state_ >> 16;
}

std::uint32_t StallDice::roll() {
    if (percent_ <= 0) return 0;
    if (static_cast<int>(next() % 100u) >= percent_) return 0;
    return 40 + next() % 61u;
}

}  // namespace sh::debug
