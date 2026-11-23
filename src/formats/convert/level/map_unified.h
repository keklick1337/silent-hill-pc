#pragma once

// The unified map file: one glTF 2.0 binary per map area,
// gamedata/maps/areas/<AREA>.glb, that holds everything the map files of the area hold.
//
//  * Scene 0 (the area): every cell (BG/<AREA>xxxx.IPD) as a root node with its model instances; the global models
//    (BG/<AREA>_GLB.PLM) are meshes shared by their instances; stand-alone libraries (BG/BG_ITEM.PLM, BG/MGR.PLM,
//    an area PLM without cells) as a root node with every model. Materials point at the PNGs of convert/textures/.
//    Each cell root has a child "<cell>/collision" whose node extension SH_collision is the decoded collision data
//    of the cell (the "collision" member of the .level.json dump, offsets in the IPD).
//  * Scenes 1..: one per map that draws the area (map_areas.h). The scene's roots are the cells and one node named
//    after the map whose children place the map's points, events, cameras, collision triggers and spawns as empty
//    nodes with extras. These are a copy for editors (Blender): gamedata/maps/<map>/map.json stays the authority for
//    the overlay data and events.lua for the handlers; the map node's extras name both files.
//  * The root extension SH_map_files lists the original files with their bytes, each in a bufferView of its own:
//    {"version": 1, "group": "ER", "files": [{"path", "kind", "size", "stored_size", "crc32", "bufferView", "node"}]}.
//    The stored bytes are those of raw/<path>: the logical file (`size` bytes, `crc32` over them, as index.tsv) and
//    the tail of its last disc sector. The game reads the map files by LBA (the original's in-memory layout),
//    so the engine serves these bytes for the paths (engine/maps/unified_layer.h): behaviour is identical and
//    raw/BG/*.IPD, *.PLM are not needed.
//
// Only BG/ is packed: TEST/*.IPD are development leftovers the game never loads.

#include "formats/common/byte_source.h"
#include "formats/common/status.h"
#include "formats/convert/level/level_gltf.h"
#include "formats/convert/level/level_textures.h"
#include "formats/convert/mapdata/mapdata.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::convert::level {

inline constexpr std::string_view kFilesExtension = "SH_map_files";
inline constexpr std::string_view kCollisionExtension = "SH_collision";
inline constexpr int kUnifiedVersion = 1;

struct SourceFile {
    std::string game_path;             // canonical: "BG/ER0000.IPD"
    std::uint64_t size = 0;            // logical size (index.tsv)
    std::vector<std::uint8_t> stored;  // raw/<path>: the logical bytes, then the tail of the last sector
};

struct MapScene {
    std::string map;  // "map0_s02"
    sh::convert::mapdata::MapData data;
};

struct UnifiedInput {
    std::string group;                  // "ER": file name of the .glb
    std::vector<SourceFile> cells;      // the area's IPDs in name order
    std::optional<SourceFile> global;   // BG/<AREA>_GLB.PLM, if the area has one
    std::vector<SourceFile> libraries;  // other PLMs packed with the group
    std::vector<MapScene> maps;         // maps that draw the area
    const TextureCatalog* catalog = nullptr;
};

struct UnifiedStats {
    GltfStats gltf;
    std::size_t files = 0;
    std::uint64_t stored_bytes = 0;
    std::size_t unresolved_instances = 0;
    std::vector<std::string> warnings;  // files whose geometry could not be parsed (their bytes are still packed)
};

// "maps/areas/ER.glb": the path under gamedata/ and inside a mod.
std::string unified_relative_path(std::string_view group);

// The group a game path is packed in: "BG/ER0000.IPD" -> "ER", "BG/THR_GLB.PLM" -> "THR", "BG/MGR.PLM" -> "MGR".
// Empty for paths that are not packed.
std::string unified_group_of(std::string_view game_path);

Result<std::vector<std::uint8_t>> write_unified(const UnifiedInput& input, UnifiedStats* stats = nullptr);

// ---- reading ---------------------------------------------------------------------------------------------------

struct UnifiedEntry {
    std::string game_path;
    std::string kind;  // "ipd", "plm"
    std::uint64_t size = 0;
    std::uint64_t stored_size = 0;
    std::uint32_t crc32 = 0;
    std::uint64_t glb_offset = 0;  // first stored byte in the .glb
};

struct UnifiedIndex {
    std::string group;
    std::vector<UnifiedEntry> files;
};

// Reads the GLB header and JSON chunk only. Fails on a file that is not a unified map file of this version.
Result<UnifiedIndex> read_unified_index(const ByteSource& glb);

// The stored bytes of one entry (size() >= entry.size). Fails when the CRC-32 of the logical bytes is wrong.
Result<std::vector<std::uint8_t>> read_unified_entry(const ByteSource& glb, const UnifiedEntry& entry);

}  // namespace sh::convert::level
