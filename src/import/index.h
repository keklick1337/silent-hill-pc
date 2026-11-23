#pragma once

// Internal to sh_import: the index.tsv text.

#include "formats/common/status.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sh::import {

// One line of index.tsv: a file of raw/.
struct IndexRow {
    std::string path;                    // game path, e.g. "CHARA/HERO.ILM"; the file is raw/<path>
    std::uint64_t size = 0;              // logical size: what the game gets when it reads the path
    std::uint32_t crc32 = 0;             // of the stored file, which for SILENT. entries includes the sector tail
    std::string source;                  // "SILENT:id=1422", "HILL:lba=39359+2152", "SLUS:file", "ISO:file"
    std::string format;                  // lower-case type: "tim", "ilm", "vab", "xa", "fmv", "exe", ...
    std::vector<std::string> converted;  // paths relative to convert/, in the order the converters were given
};

// Header line, then one line per row in the given order; tabs between columns, LF line ends.
std::string format_index(const std::vector<IndexRow>& rows);

Result<std::vector<IndexRow>> parse_index(std::string_view text);

// Eight upper-case hexadecimal digits.
std::string crc32_hex(std::uint32_t value);

}  // namespace sh::import
