// SPDX-License-Identifier: GPL-3.0-or-later
// The languages of the game are data: a language exists because a text directory holds its descriptor,
// <text>/<code>/lang.json ("sh-text-lang" v1). The base game's text directory and every
// mod's are scanned; nothing in the code lists languages.
#pragma once

#include "formats/common/status.h"
#include "i18n/core/bidi.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::i18n {

struct Language {
    std::string code;          // the directory's name; what the config and the saves keep
    std::string name;          // in the language itself, shown in Options
    std::string english_name;  // for logs and tools
    Direction direction = Direction::Ltr;
    std::vector<std::string> fonts;  // font families to draw characters the game font lacks with, best first
    int glyph_cell_height = 16;      // rows of the cells those characters are drawn into, and of a line of text
    int menu_order = 0;              // place in Options > Language after the base language: lower first, then code
    std::filesystem::path descriptor;
};

// Parses a descriptor. `code` is the directory name, which wins over the file's own "code". Unknown keys and a
// differing code go to `warnings`.
Result<Language> parse_language(std::string_view json, std::string_view code, std::vector<std::string>& warnings);

// Every language with a descriptor under the given text directories. A later directory's descriptor replaces an
// earlier one's for the same code (mods come after the base game). English first, the rest by code.
std::vector<Language> discover_languages(std::span<const std::filesystem::path> text_dirs,
                                         std::vector<std::string>& warnings);

}  // namespace sh::i18n
