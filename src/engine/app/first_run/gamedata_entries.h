// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the first run does with each top-level entry of an existing gamedata/ before it imports (first_run.cpp).
// gamedata/ holds three kinds of entries side by side: the import outputs, the base data the build copies from
// the repository's gamedata/ (cmake/copy_gamedata.cmake), and files of older builds. Only the first and the
// last may move aside: a base-data entry that moves to gamedata.legacy/ is lost to the game for good.

#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace sh::first_run {

// Written by the importer. maps/ also carries shipped base files (events.lua,
// handlers.json, ...): first_run.cpp puts back every file of it the importer did not write.
inline bool is_import_output(std::string_view name) {
    constexpr std::array<std::string_view, 6> kOutputs = {"version.cfg", "index.tsv", "raw", "convert", "tables", "maps"};
    return std::find(kOutputs.begin(), kOutputs.end(), name) != kOutputs.end();
}

// What earlier builds copied into gamedata/; the game reads these from assets/. "font" is not one of them: the
// base data ships gamedata/font/ (the kanji tables), and listing the shared name here would move it aside for good.
// tests/import/gamedata_entries_test.cpp keeps this list apart from the repository's gamedata/.
inline bool is_old_port_asset(std::string_view name) {
    constexpr std::array<std::string_view, 7> kOld = {"builtin", "decal.png", "icons", "lang", "ra", "sound", "title"};
    return std::find(kOld.begin(), kOld.end(), name) != kOld.end();
}

}  // namespace sh::first_run
