#pragma once

#include <string_view>

// The PS1 semi-transparency modes (the ABR bits 5..6 of a texture page word) as the importer names them in material
// extras ("abr", "blend_mode"). Only texels whose colour word has the STP bit blend; B is what is already drawn, F the
// new texel:
//   0 average       B / 2 + F / 2
//   1 additive      B + F
//   2 subtractive   B - F
//   3 add_quarter   B + F / 4
namespace sh::gltf {

// Every map cell, PLM library and held-item material gets this mode from the game's loaders.
inline constexpr int kMapBlendAbr = 1;

inline constexpr std::string_view blend_mode_name(int abr) {
    switch (abr & 3) {
        case 0:
            return "average";
        case 1:
            return "additive";
        case 2:
            return "subtractive";
        default:
            return "add_quarter";
    }
}

// The ABR of a name blend_mode_name gives; -1 for any other text.
inline constexpr int abr_of_blend_mode(std::string_view name) {
    for (int abr = 0; abr < 4; ++abr) {
        if (blend_mode_name(abr) == name) {
            return abr;
        }
    }
    return -1;
}

}  // namespace sh::gltf
