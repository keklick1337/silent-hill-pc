#pragma once

#include "formats/convert/anim/anm_file.h"

#include <string>
#include <string_view>

// sh-anim/1: an ANM file as JSON, without loss (anim_json_to_anm(anm_to_json(f)) serialises to the same bytes).
//
//   {"format": "sh-anim/1", "source": {"path", "size", "crc32"},
//    "kind": "anm" | "keyframes"           ("keyframes": headerless HB_M*.ANM, see anm_file.h)
//    "base": "ANIM/HB_BASE.ANM"            (keyframes only)
//    "header": {"data_offset", "rotation_slots", "translation_slots", "keyframe_size", "bone_count", "pad7",
//               "active_bones", "file_size", "keyframe_count", "scale_log2", "root_y_offset"},
//    "bones": [{"parent", "rotation", "translation", "bind": [x, y, z]}],
//    "gap": "hex", "tail": "hex",
//    "keyframes": [{"t": [[x, y, z] per translation slot], "r": [[9 values] per rotation slot]}],
//    "units": {...}}                       (how to read the numbers; ignored by the reader)
//
// Keyframe values are the stored s8: translation * 2^scale_log2 is PS1 units (Q8 metres), rotation * 32 is Q12.
namespace sh::convert::anim {

std::string anm_to_json(const AnmFile& file, std::string_view game_path, std::span<const std::uint8_t> raw,
                        std::string_view base_path);

Result<AnmFile> anm_from_json(std::string_view json_text);

}  // namespace sh::convert::anim
