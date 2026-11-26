// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/save/save_version.h"

#include <algorithm>
#include <cstddef>

namespace sh::save {

std::optional<std::vector<int>> parse_game_version(std::string_view text) {
    const auto digit = [&text](std::size_t i) { return i < text.size() && text[i] >= '0' && text[i] <= '9'; };
    std::vector<int> out;
    std::size_t pos = 0;
    while (digit(pos)) {
        int n = 0;
        int digits = 0;
        for (; digit(pos); ++pos, ++digits) {
            if (digits >= 9) {
                return std::nullopt;  // no version number is this long
            }
            n = n * 10 + (text[pos] - '0');
        }
        out.push_back(n);
        if (pos < text.size() && text[pos] == '.' && digit(pos + 1)) {
            ++pos;
        } else {
            break;
        }
    }
    if (out.empty()) {
        return std::nullopt;
    }
    return out;
}

int compare_game_versions(const std::vector<int>& a, const std::vector<int>& b) {
    const std::size_t n = std::max(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const int x = i < a.size() ? a[i] : 0;
        const int y = i < b.size() ? b[i] : 0;
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

bool from_newer_game(std::string_view saved, std::string_view current) {
    if (saved.empty()) {
        return false;
    }
    const auto now = parse_game_version(current);
    if (!now) {
        return false;  // a build without a version refuses nothing
    }
    const auto then = parse_game_version(saved);
    return !then || compare_game_versions(*then, *now) > 0;
}

}  // namespace sh::save
