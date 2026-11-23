#pragma once

#include "import/import.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sh::import_tool {

// One line of the size table: a subdirectory (or the loose files of the directory, named "(files)") with the
// number of regular files below it and their total size.
struct DirSize {
    std::string name;
    std::uint64_t files = 0;
    std::uint64_t bytes = 0;
};

// The immediate subdirectories of `dir`, sorted by name, and a "(files)" line when `dir` holds files itself.
// Empty when `dir` does not exist.
std::vector<DirSize> directory_sizes(const std::filesystem::path& dir);

// "12.3 MiB", "980 B".
std::string format_bytes(std::uint64_t bytes);

// The final report: Report numbers, per converter lines, the size tables, the list of failures and the time.
// `raw` and `converted` may be empty (a failed import leaves no directories).
std::string format_summary(const sh::import::Report& report, const std::vector<DirSize>& raw,
                           const std::vector<DirSize>& converted, double seconds);

// The failures of the report, one per line, at most `limit` of them.
std::string format_failures(const sh::import::Report& report, std::size_t limit);

}  // namespace sh::import_tool
