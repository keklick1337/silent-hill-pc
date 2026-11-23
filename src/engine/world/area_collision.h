#pragma once

// Reads the cell collision of a unified map file (maps/areas/<AREA>.glb): every
// node "<CELL>/collision" carries the decoded collision of that cell in its SH_collision extension. Only the glTF JSON
// chunk is read; the packed cell files (SH_map_files) are not used.

#include "engine/world/cell_collision.h"
#include "formats/common/status.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <array>

namespace sh::world {

// Cells by upper-case stem ("ER0402").
using AreaCollision = std::map<std::string, std::shared_ptr<const CellCollision>>;

// Parses the .glb bytes. A cell whose extension is malformed is an error naming the cell and the member.
Result<AreaCollision> read_area_collision(std::span<const std::uint8_t> glb);

// Texture names (8 bytes, NUL padded) of the cells and libraries of an area file by upper-case stem ("ER0402",
// "ER_GLB"): the extras "materials" of their root nodes (importer 7), in file order.
using AreaMaterials = std::map<std::string, std::vector<std::array<char, 8>>>;
Result<AreaMaterials> read_area_materials(std::span<const std::uint8_t> glb);

// The JSON text of one SH_collision extension (also the `collision` member of a .level.json).
Result<CellCollision> parse_cell_collision(std::string_view json_text, std::string name);

}  // namespace sh::world
