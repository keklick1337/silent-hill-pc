#pragma once

#include "formats/convert/level/level_model.h"

#include <string>
#include <string_view>

namespace sh::convert::level {

// Full dump of the parsed model as JSON (UTF-8, schema "silent-hill-level" version 1). Every structure carries its
// source offset; arrays of small records are written as rows of numbers, with the column names in a `fields` entry.
// Names are ASCII; the raw 8 bytes of every name are kept as hex next to it.
// `glb_section`, when not empty, is JSON text written as the member "glb" (what the .glb of the cell left out).
std::string ipd_to_json(const IpdFile& ipd, std::string_view game_path, std::string_view glb_section = {});
std::string plm_to_json(const LmFile& plm, std::string_view game_path);
// The "collision" member of ipd_to_json alone (a JSON object).
std::string collision_to_json(const IpdCollision& collision);

}  // namespace sh::convert::level
