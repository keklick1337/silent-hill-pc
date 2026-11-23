// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/mods/extra_config.h"

#include <algorithm>

namespace sh::mods {

namespace {

char upper(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }

std::string clipped(std::string_view s, std::size_t max) { return std::string(s.substr(0, max)); }

bool equal_ignoring_case(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return upper(x) == upper(y); });
}

}  // namespace

void ExtraConfig::store(std::string_view key, std::string_view value) {
    if (key.empty()) {
        return;
    }
    for (Entry& e : entries_) {
        if (equal_ignoring_case(e.key, key)) {
            e.value = clipped(value, kMaxValue);
            return;
        }
    }
    if (entries_.size() >= kMaxKeys) {
        return;
    }
    entries_.push_back({clipped(key, kMaxKey), clipped(value, kMaxValue)});
}

const char* ExtraConfig::value(std::string_view key) const {
    for (const Entry& e : entries_) {
        if (equal_ignoring_case(e.key, key)) {
            return e.value.c_str();
        }
    }
    return nullptr;
}

ExtraConfig& extra_config() {
    static ExtraConfig config;
    return config;
}

}  // namespace sh::mods
