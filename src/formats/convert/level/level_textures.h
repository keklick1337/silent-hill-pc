#pragma once

// Which texture file and which palette a model's primitive is drawn with, and the PNG the texture converter wrote
// for that palette. Shared by the level converter (IPD, PLM) and the model converter (ILM, held-item PLM).
//
// How the game picks them:
//  * the TIM of a map material is the first file of the file table named "<material name>.TIM"
//    (Fs_FindNextFile(name, FileType_Tim, 0) in Texture_Get); held items and characters name theirs explicitly;
//  * the TIM's CLUT block is uploaded to some VRAM position, and Material_FsImageApply stores that position as the
//    material's CLUT word; Model_MaterialFlagsApply then sets every primitive's CLUT to
//    `new base + (primitive CLUT - material CLUT base)`. So the palette a primitive uses is its CLUT word relative to
//    the material's CLUT base (LmMaterial::clut_base, 0 in every file of the image), counted inside the TIM's CLUT block: one row is
//    `block width / palette size` palettes.
//  * texture coordinates are relative to the material's UV base (uv_base) in the same way (also 0 in every file), so they are
//    texels of the TIM picture.

#include "formats/common/status.h"

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert::level {

// Reads another file of the game by its path ("BG/THR_GLB.PLM"). Empty when the file does not exist. Same signature
// as sh::convert::model::SiblingReader.
using SiblingReader = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view game_path)>;

// Header facts of a TIM that matter for texture coordinates and palettes.
struct TimInfo {
    std::string game_path;      // "BG/THR02H2F.TIM"
    int bpp = 4;                // 4, 8, 16 or 24
    int width = 256;            // pixels
    int height = 256;
    bool has_clut = false;
    int clut_words = 0;         // CLUT block width in 16-bit words
    int clut_rows = 0;
    int palette_size = 0;       // 16 or 256 words; 0 without CLUT
    int palette_count = 0;      // what the texture converter writes as <name>.png, <name>.p01.png, ...
};

// Parses the TIM header. Fails on anything the texture converter would not take as well.
Result<TimInfo> read_tim_info(std::span<const std::uint8_t> tim, std::string_view game_path);

// Palette index inside the TIM for a primitive CLUT word, relative to the material's CLUT base (LmMaterial::clut_base). -1 when the
// word points outside the TIM's CLUT block (the game then reads whatever lies in VRAM there).
int palette_index(const TimInfo& tim, std::uint16_t primitive_clut, std::uint16_t material_clut_base);

// "textures/BG/THR02H2F.png" for palette 0, "textures/BG/THR02H2F.p07.png" for palette 7: the naming of the texture
// converter (tim_converter.h), with at least two digits and more when the TIM has 101 palettes or more.
std::string palette_png(const TimInfo& tim, int palette);

// A URI for `target` (a path in the converted tree) as seen from the file `from` (also a path in the converted tree):
// glTF resolves relative URIs against the directory of the .glb. ("levels/BG/X.glb", "textures/BG/Y.png") ->
// "../../textures/BG/Y.png".
std::string relative_uri(std::string_view from, std::string_view target);

// Finds and caches TIMs by name, in file-table order, through a sibling reader. Safe to share between threads.
class TextureCatalog {
public:
    explicit TextureCatalog(SiblingReader sibling);

    // The TIM a map material named `name` loads (first in the file table), or nullptr when there is none.
    const TimInfo* by_name(std::string_view name) const;
    // The TIM at a known game path, or nullptr.
    const TimInfo* by_path(std::string_view game_path) const;

private:
    SiblingReader sibling_;
    mutable std::mutex mutex_;
    mutable std::map<std::string, std::optional<TimInfo>, std::less<>> by_name_;
    mutable std::map<std::string, std::optional<TimInfo>, std::less<>> by_path_;
};

}  // namespace sh::convert::level
