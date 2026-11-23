#pragma once

#include "formats/convert/converter.h"
#include "formats/convert/level/level_textures.h"

#include <memory>

namespace sh::convert::level {

struct Options {
    // Reads other files of the game: the area's global PLM, the TIMs of the materials (texture size and palettes) and
    // the other cells of an area. Without it the .glb files have no textures, global models stay unresolved and no
    // area .glb is written.
    SiblingReader sibling;
};

// Converter "level" (not lossless). Takes the map files: every *.IPD (BG/ and TEST/) and the model libraries under BG/
// (*.PLM). decode() gives:
//   levels/<FOLDER>/<NAME>.level.json   a full dump of the parsed structures with their source offsets; for a cell,
//                                       the member "glb" lists what the .glb left out (unresolved instances, missing
//                                       textures)
//   levels/<FOLDER>/<NAME>.glb          the cell or the model library (level_gltf.h)
//   levels/areas/<AREA>.glb             a whole map area, written by the decode of the area's first cell (lowest
//                                       name) when the area is drawn by one of the 43 maps (map_areas.h); maps that
//                                       share an area share the file, its extras list them
// encode() is not supported: the raw files stay the authority.
std::unique_ptr<sh::convert::Converter> make_converter();
std::unique_ptr<sh::convert::Converter> make_converter(Options options);

}  // namespace sh::convert::level
