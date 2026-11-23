// SPDX-License-Identifier: GPL-3.0-or-later
//
// Loading the disc's file table: the table is sh::loader::DiscFiles,
// filled at boot from the imported gamedata/tables/filetable.json, with the XA container sectors the sound code
// addresses by number. A file number is only a handle of the running table, found by its disc path.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "engine/app/dialog.h"
#include "engine/app/loader/game_files.h"
#include "engine/game_state/game_state_app.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/vfs/vfs_c.h"
#include "formats/convert/filetable/file_table.h"

namespace {

namespace ld = ::sh::loader;

constexpr int kXaBases = 9;  // the XA containers the sound code addresses by number (1..9)

ld::DiscFiles& table() { return ld::disc_files(); }

// Sectors of the first nine XA files ([1..9]); [0] and [10] are the zero ends the sound code expects.
std::uint32_t s_xa_sectors[kXaBases + 2];

[[noreturn]] void fatal(const std::string& reason) {
    SH_LOG("[FileTable] FATAL: %s", reason.c_str());
    const std::string msg = "The game's file table could not be loaded:\n\n" + reason +
                            "\n\nIt is part of the imported game data (tables/filetable.json, built from\n"
                            "raw/SLUS_007.07). Start the game with --reimport to import the disc again.";
    ShDialog_Show(1, "Silent Hill - file table missing", msg.c_str());
    std::exit(EXIT_FAILURE);
}

}  // namespace

void sh::game_state::init_file_table() {
    if (!ShVfs_GameDataActive()) {
        fatal("there is no imported game data directory.");
    }
    auto loaded = sh::convert::filetable::load_or_import(std::filesystem::path(ShVfs_GameDataDir()));
    if (!loaded) {
        fatal(loaded.error());
    }
    table().assign(std::move(loaded.value().files));

    s_xa_sectors[0] = 0;
    const int first = table().find("XA/05_02152");
    for (int i = 1; i <= kXaBases; i++) {
        s_xa_sectors[i] = first == ld::DiscFiles::kNone ? 0 : table().first_sector(first + i - 1);
    }
    s_xa_sectors[kXaBases + 1] = 0;
    SH_LOG("[FileTable] %d files from %s/tables/filetable.json", table().count(), ShVfs_GameDataDir());
}

std::uint32_t sh::loader::xa_container_sector(int base) { return s_xa_sectors[base]; }
