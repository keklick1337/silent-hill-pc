// SPDX-License-Identifier: GPL-3.0-or-later
// The quick setup's strings: gamedata/text/<lang>/setup.json (and the value names of options_menu.json, which the
// Options screen shows the same choices with), English under every language. Read from the base data beside the
// executable, so they are there before anything is imported.
#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>

namespace sh::quick_setup {

class Strings {
public:
    // English first, then `code` over it.
    void load(const std::filesystem::path& text_root, std::string_view code);

    bool has(std::string_view key) const;
    // The key itself when no file has it, so a missing string shows rather than vanishes.
    std::string get(std::string_view key) const;
    // {0}, {1}, ... replaced; other braces left as they are.
    std::string format(std::string_view key, std::initializer_list<std::string_view> args) const;

private:
    void read(const std::filesystem::path& file);
    std::unordered_map<std::string, std::string> strings_;
};

// Fills {0}, {1}, ... of `pattern`.
std::string fill(std::string_view pattern, std::initializer_list<std::string_view> args);

}  // namespace sh::quick_setup
