// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The config keys the game's own parser did not recognize: config.cfg lines kept by name so the modding tools can
// read their settings back (editor_fog_off, editor_ui_scale, ...).

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sh::mods {

class ExtraConfig {
public:
    static constexpr std::size_t kMaxKeys = 128;
    static constexpr std::size_t kMaxKey = 47;
    static constexpr std::size_t kMaxValue = 79;

    // Keys compare ASCII case-insensitively; a key stored again takes the new value.
    void store(std::string_view key, std::string_view value);
    // nullptr when the key was not in the config.
    const char* value(std::string_view key) const;

private:
    struct Entry {
        std::string key;
        std::string value;
    };
    std::vector<Entry> entries_;
};

ExtraConfig& extra_config();

}  // namespace sh::mods
