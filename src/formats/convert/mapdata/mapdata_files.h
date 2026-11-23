#pragma once

// gamedata/maps/<map>/<kind>.json from gamedata/raw/VIN/MAPn_Snn.BIN (sh-import, the first run, and the game when a
// file is missing), checked against the disc.

#include "formats/convert/mapdata/mapdata.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sh::convert::mapdata {

// "maps/map1_s02/spawns.json": the path under gamedata/ and inside a mod.
std::filesystem::path map_file_path(std::string_view map, Kind kind);

// The overlay's bytes from <gamedata_dir>/raw/.
Result<std::vector<std::uint8_t>> read_overlay(const std::filesystem::path& gamedata_dir, const MapLayout& layout);

struct GenerateReport {
    unsigned maps = 0;
    unsigned written = 0;        // files
    unsigned kept = 0;           // existing files left as they are (import_overwrite rewrites them)
    unsigned failed = 0;         // a map whose overlay is missing or broken, a file that could not be written,
                                 // or JSON that does not give the disc bytes back
    unsigned round_trips = 0;    // kinds whose fresh JSON gave the disc bytes back
    unsigned files_checked = 0;  // files in gamedata/maps compared with the disc
    unsigned files_differ = 0;   // of those, files with other values than the disc (edited by hand)
    std::vector<std::string> warnings;
};

// Writes the files of every map, then reads each file back and compares it with the disc.
GenerateReport generate_map_files(const std::filesystem::path& gamedata_dir, bool overwrite);

// The same for one map.
void generate_map_files(const std::filesystem::path& gamedata_dir, const MapLayout& layout, bool overwrite,
                        GenerateReport& report);

// "43 maps, 258 written, 0 kept; disc check: 258 round trips, 258 files, 0 differ"
std::string format_report_line(const GenerateReport& report);

// Writes `text` to a temporary name beside `path` and renames it.
Result<void> write_file_atomic(const std::filesystem::path& path, const std::string& text);

}  // namespace sh::convert::mapdata
