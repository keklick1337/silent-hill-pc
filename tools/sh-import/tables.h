#pragma once

// gamedata/tables/ after an import (import_tables = json): every table is written as JSON from the disc (raw/),
// with <name>@<overlay>.json for an overlay whose copy differs, unless the file is already there (import_overwrite
// rewrites it); then each copy in raw/ is compared with the file the game uses for that overlay.
// The tables come from the disc import only: without raw/ every table is a failure, and none is written from
// numbers compiled into the executable.

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

namespace sh::import_tool {

struct TablesReport {
    unsigned written = 0;  // files, variants included
    unsigned kept = 0;
    unsigned failed = 0;          // could not be written or read back
    unsigned copies_checked = 0;  // table copies in raw/ compared
    unsigned copies_differ = 0;
    unsigned entries_differ = 0;
    std::vector<std::string> warnings;  // one line per problem or differing entry
};

TablesReport generate_tables(const std::filesystem::path& gamedata_dir, bool overwrite);

// "38 written, 0 kept; raw/ check: 70 copies, 0 differ in 0 entries"
std::string format_tables_line(const TablesReport& report);

}  // namespace sh::import_tool
