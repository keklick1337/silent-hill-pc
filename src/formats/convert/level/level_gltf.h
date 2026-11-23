#pragma once

// glTF 2.0 binary (.glb) of map cells, model libraries and whole map areas, from the flat meshes of level_flat.h.
//
// Materials. One glTF material per (texture, palette, semi-transparency). The texture is the PNG the texture converter
// wrote for the palette the primitives really use (level_textures.h), referenced relative to the .glb. Opaque
// textured materials use alphaMode MASK: the PS1 never draws a texel whose colour word is 0x0000, and that word is
// alpha 0 in the PNG. Semi-transparent primitives use BLEND at half alpha; the game's blend equation (additive for
// maps) is not expressible in glTF core and is named in the material extras. A material whose texture file is not on
// the disc (DWAVE, the water surface, is drawn into VRAM at run time) has no texture; extras say so.
//
// Units, axes and winding are those of level_flat.h (and of the model converter).

#include "formats/common/status.h"
#include "formats/convert/level/level_flat.h"
#include "formats/convert/level/level_model.h"
#include "formats/convert/level/level_textures.h"
#include "formats/gltf/gltf_writer.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace sh::convert::level {

struct GltfStats {
    std::size_t meshes = 0;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    std::size_t materials = 0;
    std::size_t textured_materials = 0;
    std::size_t palettes_outside_tim = 0;    // materials whose CLUT points outside their TIM's CLUT block
    std::set<std::string> missing_textures;  // material names without a TIM on the disc
};

// Builds one document. Not thread-safe; one builder per output file.
class GltfBuilder {
public:
    // `glb_path` is the place of the .glb in the converted tree ("levels/BG/THR0205.glb"); texture URIs are made
    // relative to it. `catalog` may be null: then no material has a texture and texture coordinates assume 256 x 256.
    GltfBuilder(std::string glb_path, const TextureCatalog* catalog);

    // Prefix of the texture PNG paths before the URI is made relative to `glb_path`: "" when the .glb lies in the
    // converted tree (convert/), "convert/" when `glb_path` is relative to gamedata/ (maps/areas/ER.glb).
    void set_texture_root(std::string root) { texture_root_ = std::move(root); }

    // A mesh with one primitive per flat mesh; -1 when the model has no triangles.
    int add_mesh(const FlatModel& model);
    // Appends a node under `parent` (-1: a root of the scene) and returns its index.
    int add_node(gltf::Node node, int parent = -1);

    gltf::Document& document() { return doc_; }
    const GltfStats& stats() const { return stats_; }
    Result<std::vector<std::uint8_t>> write() const;

private:
    int material_for(const FlatMesh& mesh, const TimInfo** tim);

    std::string glb_path_;
    std::string texture_root_;
    const TextureCatalog* catalog_;
    gltf::Document doc_;
    GltfStats stats_;
    std::map<std::string, int> texture_index_;
    std::map<std::tuple<std::string, int, bool>, int> material_index_;
};

struct CellGlb {
    std::vector<std::uint8_t> glb;
    std::string json_section;  // compact JSON for the "glb" member of the cell's level.json
    GltfStats stats;
    std::size_t instances = 0;            // nodes with a mesh or a global reference
    std::size_t global_references = 0;    // instances of models of the area's global PLM (nodes without a mesh)
    std::size_t unresolved = 0;           // instances the game skips: their model is nowhere to be found
};

// One cell. Models of the cell's own LM become meshes; instances of the global PLM (`global`, read from
// `global_path`) become empty nodes whose extras name the model and the PLM's .glb, so the global geometry is not
// copied into every cell; instances whose model cannot be found are left out and listed in json_section.
Result<CellGlb> cell_to_glb(const IpdFile& ipd, std::string_view game_path, const LmFile* global,
                            std::string_view global_path, const TextureCatalog* catalog);

// Every model of a stand-alone LM as a node at the origin. `glb_path` decides the texture URIs.
Result<std::vector<std::uint8_t>> plm_to_glb(const LmFile& plm, std::string_view game_path, std::string_view glb_path,
                                             const TextureCatalog* catalog, GltfStats* stats = nullptr);

struct AreaCell {
    std::string game_path;  // "BG/THR0205.IPD"
    IpdFile ipd;
};

// A whole map area: every cell as a node (instances already carry the cell origin), global models included once and
// shared by all their instances. `maps` lists the maps that draw the area (asset extras).
Result<std::vector<std::uint8_t>> area_to_glb(std::string_view area, const std::vector<AreaCell>& cells,
                                              const LmFile* global, std::string_view global_path,
                                              const std::vector<std::string>& maps, const TextureCatalog* catalog,
                                              GltfStats* stats = nullptr);

// The pieces of area_to_glb and plm_to_glb, for documents that hold more (map_unified.h).
// Every model of `plm` as a child of a new root node named after the file; returns the root. `built` (may be null):
// meshes already in the document by model name (the global models the cells instance), reused and extended.
int add_library_nodes(GltfBuilder& builder, const LmFile& plm, std::string_view game_path,
                      std::map<std::string, int>* built = nullptr);
// Every cell as a root node with its instances and a "<CELL>/models" group with every model of its LM by name (what
// the game draws by name: doors, items, props); `cell_roots` (may be null) gets the root of each cell in order,
// `global_meshes` (may be null) the meshes made for the global models by name. Returns the number of instances whose
// model was not found.
std::size_t add_area_nodes(GltfBuilder& builder, const std::vector<AreaCell>& cells, const LmFile* global,
                           std::vector<int>* cell_roots, std::map<std::string, int>* global_meshes = nullptr);

}  // namespace sh::convert::level
