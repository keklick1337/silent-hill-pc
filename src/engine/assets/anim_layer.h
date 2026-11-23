#pragma once

// The engine reads the game's animations from convert/anim/**/*.anim.json (sh-anim/1, lossless: anim_json.h) when
// anim_source = json: a VFS layer that answers ANIM/<NAME>.ANM with the bytes rebuilt from
// convert/anim/ANIM/<NAME>.anim.json. The game loads ANM files by LBA (DiscSectors), so a path the layer has replaces
// the sectors of raw/ and Anim_BonesUpdate sees exactly the bytes of the disc file.
//
// A disc file's last sector holds bytes past its logical size (the rest of the sector). The JSON has only the
// file, so the layer takes that tail from `tail_from` (gamedata/raw) when it has the same file size; without it the
// tail reads as zeros, which no code of the game reads.

#include "engine/vfs/vfs.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace sh::assets {

// level 0: info, 1: warning.
using AssetLogFn = std::function<void(int level, const std::string& message)>;

// Null when <root>/convert/anim holds no *.anim.json. Files are parsed on first read; a broken one is logged and
// answers as missing, so the layers below give the file.
std::unique_ptr<sh::vfs::Layer> make_anim_json_layer(const std::filesystem::path& root, std::string name,
                                                     const sh::vfs::Layer* tail_from, const AssetLogFn& log);

// Game path of a converted animation: "ANIM/HB_BASE.anim.json" (relative to convert/anim) -> "ANIM/HB_BASE.ANM".
// Empty when the name does not end in .anim.json.
std::string anim_game_path(const std::filesystem::path& relative);

// An import made before the anim converter has no convert/anim: writes it once from the ANM files of `raw`
// (index.tsv paths). Returns the number of files written.
int write_anim_json_from_raw(const std::filesystem::path& gamedata_dir, const sh::vfs::Layer& raw,
                             const AssetLogFn& log);

}  // namespace sh::assets
