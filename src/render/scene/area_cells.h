#pragma once

// Drawable cells of the unified map files (maps/areas/<AREA>.glb): each cell root node with
// its model instances baked into one indexed triangle list in PS1 axes relative to the cell corner, grouped by
// material, and the textures of those materials with the PS1 semi-transparency bit (STP) restored from the TIM's
// palettes (convert/textures/**/<TIM>.tim.json), which the PNG does not carry.

#include "formats/common/status.h"
#include "render/scene/scene.h"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh::scene {

struct CellVertex {
    float x = 0, y = 0, z = 0;     // PS1 axes, 256 per metre, relative to the cell corner (cell_x * 40 m)
    float nx = 0, ny = 0, nz = 0;  // inward unit normal (the PS1 light rules are written for it), PS1 axes
    float u = 0, v = 0;
    // UV box of the triangle this vertex belongs to (normalised like u, v), so the sampler never reads texels the PS1
    // polygon does not show: the neighbouring cells of the texture atlas. hi < lo = no box (the shader samples the
    // image as is).
    float box_u0 = 0, box_v0 = 0, box_u1 = -1, box_v1 = -1;
};
static_assert(sizeof(CellVertex) == 48);

struct CellMaterial {
    std::string texture;     // gamedata path of the PNG ("convert/textures/BG/ER_CHRC2.p09.png"); empty = white
    int palette = -1;        // the TIM palette the PNG shows (STP lookup), -1 unknown
    // A material without a picture file (extras "material" with an empty "tim": DWAVE, the sewer water): its name.
    // The one scene::water_surface() names is drawn by the water shader. Empty for every other material.
    std::string unpictured;
    bool semi_transparent = false;
    // How the STP texels of a semi-transparent material blend: extras "abr" / "blend_mode" of the importer; without
    // them (an import before importer 10, a modder's file) the map loaders' additive mode.
    Blend blend = Blend::Additive;
    // UVs are PS1 texel coordinates + 0.5 over the image size (the importer's convention, level_flat.cpp); such
    // triangles get a UV box. A modder's own material makes no such promise and is sampled as authored.
    bool texel_center_uv = false;
};

// Layers of surfaces lying on one another in the same plane: 0 is the ground, each next one lies on the one before.
inline constexpr int kMaxOverlayLayer = 7;

struct CellRange {
    int material = 0;        // index into CellMesh::materials
    std::uint32_t first = 0; // first index
    std::uint32_t count = 0;
    // 0: an ordinary surface. Above 0: an overlay (a road marking, a poster, a painted shadow) sharing its plane with
    // the surface it covers; the renderer draws it at its exact depth, nearer by the layer, so it always wins
    // against what it lies on (RENDER.md section 6.6).
    int layer = 0;
};

struct CellMesh {
    std::string stem;
    int cell_x = 0;          // the cell the file was authored for
    int cell_z = 0;
    std::vector<CellVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<CellMaterial> materials;
    std::vector<CellRange> ranges;  // opaque materials first
    // Seam lines: the edges of opaque triangles that no other triangle of the cell shares, as line-list ranges (per
    // material) into `indices` after the triangles. Drawn under the triangles, they fill the sub-pixel cracks the
    // data leaves along such edges (T-junctions, models that only nearly meet) that the PS1 hid by snapping to whole
    // pixels and that show the fog behind as light lines at PC resolutions.
    std::vector<CellRange> seams;
    // Read from a mod's file: drawn as the mod made it, never through model upscaling (an HD mesh has its own shape).
    bool from_mod = false;
};

// An 8-bit RGBA texture for the scene: alpha 0 = transparent texel, 128 = STP set, 255 = solid.
struct SceneTexture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    // An upscaled picture (texture_upscale): drawn with a mip chain, so far surfaces do not shimmer.
    bool mipmapped = false;
    // Its mip_levels() when they were made ahead of the upload (prefetch_textures); empty: made at the upload.
    std::vector<SceneTexture> levels;
};

// The levels below `t` for a mip chain: each half the one before (sides rounded down, at least 1), colour averaged
// over the covered texels, coverage and the STP class by majority. Stops at 1x1.
std::vector<SceneTexture> mip_levels(const SceneTexture& t);

// `t` with neither side over `max_side`: unchanged when it fits (or max_side <= 0), else scaled down with its aspect
// kept, each texel the average of the texels it covers. Alpha keeps its three classes (0, 128, 255).
SceneTexture fit_texture(const SceneTexture& t, int max_side);

// Parses one area file; every cell root becomes a CellMesh. `area_path` only names errors.
Result<std::vector<CellMesh>> read_area_cells(std::span<const std::uint8_t> glb, std::string_view area_path);

// A model of a library that the game draws by name (a world object, the held item): its mesh in its own space (PS1
// axes and units, no cell offset).
struct LibraryModel {
    std::string name;        // "<ROOT>/<MODEL>": "ER0102/DOOR_HID", "BG_ITEM/DRINK_NE", "HANDGUN/HANDGUN"
    std::string index_name;  // "<ROOT>/#<model_index>", empty without one
    int ot_group = 0;        // the model's OT bucket group (0..5)
    CellMesh mesh;
};

// The libraries of a glb: the "<CELL>/models" group of each cell root of an area file, and every root naming a
// source file without a cell (a PLM: BG_ITEM, the global PLM of an area, ITEM/HANDGUN.glb). A glb with neither (a
// mod's own model: metres, +Y up, front +Z) is one model of all its meshes, "<FILE STEM>/#0"; a picture embedded in
// it is named "<path>#<image index>" (glb_embedded_image). `path` names the file.
Result<std::vector<LibraryModel>> read_glb_models(std::span<const std::uint8_t> glb, std::string_view path);

// The bytes of picture `index` embedded in a glb (its buffer view); nullopt when there is none.
std::optional<std::vector<std::uint8_t>> glb_embedded_image(std::span<const std::uint8_t> glb, int index);

// Decodes a material texture. `png` is the image, `tim_json` its TIM description (may be empty: no STP).
Result<SceneTexture> decode_scene_texture(std::span<const std::uint8_t> png, std::span<const std::uint8_t> tim_json,
                                          int palette);

// The TIM description next to a palette PNG: "convert/textures/BG/X.p09.png" -> "convert/textures/BG/X.tim.json".
std::string tim_json_path(std::string_view png_path);

}  // namespace sh::scene
