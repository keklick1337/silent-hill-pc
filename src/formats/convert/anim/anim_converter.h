#pragma once

#include "formats/convert/converter.h"

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Animation converter "anim": ANIM/<name>.ANM -> anim/ANIM/<name>.anim.json (sh-anim/1, anim_json.h), lossless.
// Harry's headerless map files (HB_M*.ANM) are read in the layout of ANIM/HB_BASE.ANM, which the sibling reader
// must supply; empty files (seven on the disc) are not accepted.
// The skinned, animated glTF of a character is written by the model converter (model_converter.h).
namespace sh::convert::anim {

using SiblingReader = std::function<std::optional<std::vector<std::uint8_t>>(std::string_view game_path)>;

std::unique_ptr<sh::convert::Converter> make_converter(SiblingReader sibling);

// The file whose layout Harry's headerless keyframe files share, and his map files of that kind as the disc lists
// them (five of them are empty on the disc). The model converter packs them into HERO.glb.
inline constexpr std::string_view kHarryBaseAnm = "ANIM/HB_BASE.ANM";
std::span<const std::string_view> harry_map_keyframe_files();

}  // namespace sh::convert::anim
