#pragma once

#include "formats/convert/model/model_converter.h"

#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>

// Internal to sh_convert_model: builders for the two source formats and the helpers they share.
namespace sh::convert::model {

Result<ConvertedModel> build_ilm(std::span<const std::uint8_t> raw, std::string_view game_path,
                                 const Options& options);
Result<ConvertedModel> build_tmd(std::span<const std::uint8_t> raw, std::string_view game_path,
                                 const Options& options);
Result<ConvertedModel> build_plm(std::span<const std::uint8_t> raw, std::string_view game_path,
                                 const Options& options);
bool looks_like_plm(std::span<const std::uint8_t> raw);

// PS1 geometry units per metre (Q8); see the coordinate notes in model_converter.h.
constexpr double kUnitsPerMetre = 256.0;

// PS1 (x right, y down, z forward) to glTF (y up, character faces +z): a rotation by 180 degrees about Z.
inline std::array<float, 3> to_gltf_position(double x, double y, double z) {
    return {static_cast<float>(-x / kUnitsPerMetre), static_cast<float>(-y / kUnitsPerMetre),
            static_cast<float>(z / kUnitsPerMetre)};
}

// The same rotation for a direction; the result is normalised, a zero vector becomes +Y.
inline std::array<float, 3> to_gltf_direction(double x, double y, double z) {
    const double len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-12) {
        return {0.0f, 1.0f, 0.0f};
    }
    return {static_cast<float>(-x / len), static_cast<float>(-y / len), static_cast<float>(z / len)};
}

// The three corners of a PS1 face in glTF winding. Corner order of a PS1 triangle is (0,1,2), of a quad strip
// (0,1,2)+(1,2,3); both are clockwise on screen and are reversed here.
inline std::array<std::array<int, 3>, 2> face_triangles(bool quad) {
    // Second triangle is only meaningful for a quad.
    if (quad) {
        return {{{0, 2, 1}, {1, 2, 3}}};
    }
    return {{{0, 2, 1}, {0, 0, 0}}};
}

std::string hex_bytes(std::span<const std::uint8_t> bytes);
std::string upper_copy(std::string_view text);

// "CHARA/HERO.ILM" -> "CHARA/HERO"; backslashes are treated as slashes, case is kept.
std::string path_without_extension(std::string_view game_path);
std::string path_directory(std::string_view game_path);  // "CHARA/HERO.ILM" -> "CHARA", "" when none
std::string path_stem(std::string_view game_path);       // "CHARA/HERO.ILM" -> "HERO"

}  // namespace sh::convert::model
