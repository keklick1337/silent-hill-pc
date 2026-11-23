#pragma once

// gamedata/maps/areas/<AREA>.glb (map_unified.h) from gamedata/raw/ after the import (sh-import, the first run, and
// the game when the files are missing), each checked against raw/ byte for byte.

#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace sh::convert::level {

struct UnifiedReport {
    unsigned groups = 0;
    unsigned written = 0;
    unsigned kept = 0;          // existing files left as they are (import_overwrite rewrites them)
    unsigned failed = 0;        // a group that could not be written, or a file whose bytes do not match raw/
    unsigned files = 0;         // original files packed
    unsigned round_trips = 0;   // of those, files read back from the .glb equal to raw/
    std::uint64_t glb_bytes = 0;
    std::vector<std::string> warnings;
};

// Writes every group whose files are in index.tsv under BG/, then reads each .glb back and compares every packed
// file with raw/. `threads` 0: one per hardware thread.
UnifiedReport generate_unified_maps(const std::filesystem::path& gamedata_dir, bool overwrite, unsigned threads = 0);

// "16 areas, 16 written, 0 kept; 509 files packed, 509 round trips; 80.1 MiB"
std::string format_unified_line(const UnifiedReport& report);

}  // namespace sh::convert::level
