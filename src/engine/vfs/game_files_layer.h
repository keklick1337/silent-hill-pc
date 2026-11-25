#pragma once

// The game's files of an import without raw/ (formats/convert/gamefiles/game_files.h) as a VFS layer: each path of
// convert/files/files.tsv answers with the bytes the store rebuilds, reads from maps/areas or keeps, and
// open_stored() adds the sector tail the disc had past the logical size, so reads by LBA see the disc's bytes.

#include "engine/vfs/vfs.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace sh::vfs {

// level 0: info, 1: warning.
using GameFilesLogFn = std::function<void(int level, const std::string& message)>;

// Null with `error` set when the gamedata/ has no store.
std::unique_ptr<Layer> make_game_files_layer(const std::filesystem::path& gamedata_dir, GameFilesLogFn log,
                                             std::string& error);

}  // namespace sh::vfs
