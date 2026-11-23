#pragma once

#include "formats/common/status.h"
#include "formats/png/sh_png.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::png {

// Layout (importer 3): a 4/8-bit TIM is kept as "indexed/CHARA/HERO.png" (pixels = CLUT indices, PLTE/tRNS =
// row 0) plus every CLUT row in "textures/CHARA/HERO.tim.json"; "textures/CHARA/HERO.png" is a true-colour
// picture (baked, not indexed). Importer 2 had no indexed/ directory: textures/CHARA/HERO.png itself was the
// indexed picture. A palette variant, "textures/CHARA/HERO.p03.png", is not a file in either layout: it is the
// indexed picture recoloured with CLUT row 3. A file with that name, put there by a mod, wins.

struct VariantName {
    std::string base_png;     // "textures/CHARA/HERO.png"
    std::string indexed_png;  // "indexed/CHARA/HERO.png" (indexed_path_for(base_png); empty without textures/)
    std::string tim_json;     // "textures/CHARA/HERO.tim.json"
    int row = 0;              // 3
};

// Splits "<dir>/NAME.pNN.png" (NN = 2 to 4 digits). Empty when the name is not a variant name.
std::optional<VariantName> parse_variant_name(std::string_view path);

// The indexed source of a texture: the last "textures/" path component becomes "indexed/" and everything after
// the first dot of the file name is dropped, so "gamedata/convert/textures/CHARA/HERO.p03.png" and
// ".../textures/CHARA/HERO.png" both give "gamedata/convert/indexed/CHARA/HERO.png". Empty when the path has no
// "textures" directory component.
std::string indexed_path_for(std::string_view texture_png);

// The indexed PNG `base_png` recoloured with palette `row` of the TIM description `tim_json`. Colours and the
// alpha rule are the importer's (5-bit expansion, word 0x0000 is transparent). Fails when the picture is not
// indexed, the row does not exist, or the JSON is malformed.
Result<std::vector<std::uint8_t>> make_variant_png(const std::vector<std::uint8_t>& base_png,
                                                   const std::vector<std::uint8_t>& tim_json, int row);

// The same recolouring as RGBA8 pixels, for runtime CLUT effects (no PNG round trip). A TIM without a CLUT (no
// "palettes" in the JSON) has only row 0, which is the PNG's own palette (the importer's gray ramp).
Result<Rgba8> make_row_rgba(std::span<const std::uint8_t> indexed_png, std::span<const std::uint8_t> tim_json,
                            int row);

using ByteReader = std::function<std::optional<std::vector<std::uint8_t>>(const std::string&)>;

// reader(path); when that finds nothing and `path` is a variant name, the variant is built from the indexed
// picture (indexed_png, else an indexed base_png of the importer-2 layout) and the JSON that `reader` finds.
// nullopt when neither exists.
std::optional<std::vector<std::uint8_t>> read_texture_or_variant(const std::string& path, const ByteReader& reader);

}  // namespace sh::png
