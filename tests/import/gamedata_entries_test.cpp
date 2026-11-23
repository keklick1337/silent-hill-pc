// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/app/first_run/gamedata_entries.h"
#include "framework/sh_test.h"

#include <filesystem>
#include <string>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace fs = std::filesystem;

// The first run moves old builds' files and stale import outputs aside to gamedata.legacy/; a shipped base-data
// entry with one of those names would go with them and never come back.
SH_TEST(first_run_never_moves_shipped_gamedata_aside) {
    const fs::path shipped = fs::path(SH_PROJECT_ROOT) / "gamedata";
    std::error_code ec;
    int entries = 0;
    for (fs::directory_iterator it(shipped, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        ++entries;
        SH_CHECK(!sh::first_run::is_old_port_asset(name));
        // maps/ is the one import output with base files in it, and the first run restores exactly that one.
        SH_CHECK(!sh::first_run::is_import_output(name) || name == "maps");
    }
    SH_CHECK(entries > 0);
    SH_CHECK(!sh::first_run::is_old_port_asset("font"));
    SH_CHECK(sh::first_run::is_old_port_asset("sound"));
    SH_CHECK(sh::first_run::is_import_output("maps"));
}
