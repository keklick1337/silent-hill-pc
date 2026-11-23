#pragma once

#include "formats/common/status.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Game files packed byte for byte into a .glb under a root extension of the importer (a "files" array of
// {path, kind, size, crc32, bufferView}), such as SH_anim_files of the character models.
namespace sh::gltf {

struct PackedFile {
    std::string path;  // game path, "ANIM/HB_M1S00.ANM"
    std::string kind;  // what the extension says the file is, may be empty
    std::vector<std::uint8_t> bytes;
};

// The files of `extension` in `glb`. Fails when the file is not a glb, has no such extension, or a file's bytes do
// not match its recorded size and CRC-32.
Result<std::vector<PackedFile>> read_packed_files(std::span<const std::uint8_t> glb, std::string_view extension);

}  // namespace sh::gltf
