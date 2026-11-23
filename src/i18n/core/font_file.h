// SPDX-License-Identifier: GPL-3.0-or-later
// What a TrueType/OpenType file says about itself, read without loading it: the faces of the file (a collection
// holds several), their family and style names, weight, and the characters they have. Only the table directory and
// the `name`, `OS/2`, `post` and `cmap` tables are read, so a catalogue of a whole system's fonts costs a few
// small reads per file.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace sh::i18n {

struct CodeSpan {
    char32_t first = 0;
    char32_t last = 0;
    bool operator==(const CodeSpan&) const = default;
};

struct FontFace {
    std::filesystem::path file;
    int index = 0;            // which face of a collection
    std::uint32_t offset = 0; // of the face's table directory in the file
    std::string family;
    std::string style;
    int weight = 400;         // 100..900, 400 regular, 700 bold
    bool italic = false;
    bool monospace = false;
    std::vector<CodeSpan> coverage;  // sorted, merged

    bool has(char32_t cp) const;
};

// The faces of a font file; empty when the file cannot be read or is not a font of these formats.
std::vector<FontFace> read_font_faces(const std::filesystem::path& file);

// The same from bytes already in memory (the tests' fonts); `file` is only recorded.
std::vector<FontFace> parse_font_faces(std::span<const std::uint8_t> bytes, const std::filesystem::path& file);

}  // namespace sh::i18n
