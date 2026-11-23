// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// Reading PS1 memory card images and taking the Silent Hill saves out of them.
// The game itself never touches a card image: this is the one-way import of saves written by older builds of the
// port (gamedata/save/N.MCD) or by a console or an emulator (.mcr/.mcd, DexDrive .gme).

#include "engine/save/save_store.h"
#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace sh::save {

// A file on the card: its directory name and the bytes of its blocks in chain order.
struct CardFile {
    std::string name;
    std::uint32_t size = 0;
    std::vector<std::uint8_t> data;
};

// Raw 128 KiB images, and the DexDrive (.gme) and VGS (.mem) wrappers around them.
Result<std::vector<CardFile>> read_memory_card(std::span<const std::uint8_t> image);

// Silent Hill keeps up to 15 files per card, each one block: a list of 11 save entries, the options and 11 saves.
// Region prefixes of the file name; the number after "SILENT" is the file group.
bool is_silent_hill_file(const std::string& name, int* group);

struct ImportStats {
    int files = 0;            // Silent Hill files found on the card
    int saves = 0;            // saves written to the store
    int already_there = 0;    // identical save already in the store
    int damaged = 0;          // files whose list of entries fails its checksum, saves whose record fails its own
    std::vector<std::string> notes;
};

// Adds the Silent Hill saves of a card image to the store as new saves, oldest first; a save the store already
// has (same summary and game state) is skipped. Each save carries the options of its card file.
Result<ImportStats> import_memory_card(std::span<const std::uint8_t> image, SaveStore& store);

// The older memory card files before .savegames/: <dir>/N.MCD, N = port * 8 + multitap slot. Each image is
// imported once (root/migration.json remembers its path, size and CRC-32); the files are left where they are.
struct MigrationReport {
    int sources = 0;  // card images (or old layout saves) found
    int imported = 0;  // of them, imported in this run
    int saves = 0;
    std::vector<std::string> lines;  // what happened, for the log
};

MigrationReport migrate_legacy_cards(SaveStore& store, const std::vector<std::filesystem::path>& dirs);

// Card images a player drops into root/import/ (or its card1/ card2/ of older builds; .mcd .mcr .mc .gme .mem
// .srm): each imported once, like the old files.
MigrationReport import_drop_folder(SaveStore& store);

// The two-card layout of older builds (card1|card2/slot_FF_SS.sav with the options in file_FF.sav): every save is
// copied once into a save_NNNN.sav, oldest first; the old files stay where they are.
MigrationReport migrate_card_layout(SaveStore& store);

}  // namespace sh::save
