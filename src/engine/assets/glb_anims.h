#pragma once

// Animation files packed into the character models (the importer's SH_anim_files extension of a converted glb,
// model_converter.h): the base ANM of a skeleton and the keyframe-only files in its layout (Harry's map files),
// byte for byte. The engine takes a file from the glb that packs it, so the keyframes the logic reads come from the
// model that is drawn; the glb's glTF animations are a float decomposition for viewers and are not read.

#include "formats/common/status.h"
#include "formats/convert/anim/anm_file.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace sh::assets {

inline constexpr std::string_view kAnimFilesExtension = "SH_anim_files";

// Game path ("ANIM/HB_M1S00.ANM") -> the glb that packs it, relative to convert/ ("models/CHARA/HERO.glb"), for
// every .glb below <convert_dir>/models. Only the JSON chunk of each file is read.
std::map<std::string, std::string, std::less<>> packed_anim_index(const std::filesystem::path& convert_dir);

struct PackedAnm {
    sh::convert::anim::AnmFile file;
    std::string base;  // game path of the file a keyframe-only file extends, empty for a file with a header
};

// File `game_path` of the glb `glb_file`; a keyframe-only file is read in the layout of the file the glb packs as
// kind "anm". Fails when the glb does not pack it (or it does not parse).
Result<PackedAnm> packed_anm(const std::filesystem::path& glb_file, std::string_view game_path);

}  // namespace sh::assets
