#pragma once

// Item models drawn by the scene as posed meshes (RENDER.md section 6.1): the inventory carousel, the pickup screen
// and the map5_s01 lock reels. A model is one TMD object of an item file as the importer converted it:
// convert/models/ITEM/<NAME>.glb (node obj<N>) with its sidecar <NAME>.model.json (the packets of each object and
// where their triangles are in the glb, importer 7). Textures are the import's TIM pictures (convert/indexed/ plus
// the CLUT rows of the .tim.json), coloured the way the PS1 path coloured the VRAM it sampled.
//
// What is drawn is what the PS1 drawer drew for these models: every face unlit at the neutral colour (the untextured
// ones in their packet colour), darkened by the carousel's per-slot dim, back faces culled on screen unless the face
// is double sided, no world fog and no flashlight. Positions are the transform of the vertex in floating point (the
// precision the PS1 path got from its precise projection); the projection is the one the caller gives (the item
// camera's, or the world's for the lock reels).

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

namespace sh::scene {

// Where a texture page and CLUT of a model sample a TIM picture.
struct ItemSheet {
    std::string tim;      // game path, "ITEM/TIM07.TIM"
    int palette = 0;      // CLUT row inside the TIM
    int u0 = 0, v0 = 0;   // page texel of the picture's top-left corner
};

struct ItemMaterial {
    bool textured = false;
    std::uint16_t tpage = 0;   // as the packets carry them
    std::uint16_t clut = 0;
    // The picture the import resolved; none for a page whose TIM depends on the running game (the key items' sheet
    // every map group fills with its own TIM): the caller's resolver names it.
    std::optional<ItemSheet> sheet;
    // A mod's model with a picture of its own (the glb's embedded base colour, RGBA): drawn with the faces' float
    // texture coordinates instead of page texels.
    std::shared_ptr<const std::vector<std::uint8_t>> own_rgba;
    int own_width = 0, own_height = 0;
};

struct ItemFace {
    int material = 0;
    bool semi = false;          // the face blends (packet mode bit 1)
    bool double_sided = false;  // packet flag bit 1
    // Corners in the packet's order (the PS1 front face is clockwise on screen): model units, PS1 axes.
    std::array<std::array<float, 3>, 3> position{};
    std::array<std::array<std::uint8_t, 2>, 3> texel{};  // texture page texels
    std::array<std::uint8_t, 3> color{128, 128, 128};    // untextured faces: the packet colour
    std::array<std::array<float, 2>, 3> uv{};            // faces of an own picture: its coordinates
};

struct ItemMesh {
    // False when the object holds a packet kind the item drawer does not draw (anything but lit gouraud triangles):
    // the PS1 path drew nothing of such a model either.
    bool drawable = true;
    std::vector<ItemMaterial> materials;
    std::vector<ItemFace> faces;
    // A mod's own model (centred and scaled on load): drawn units per metre of the file and the middle of its bounds
    // in the file's metres (+Y up); the faces are turned 180 degrees about Z from the file. own_scale 0: a game model.
    float own_scale = 0.0f;
    std::array<float, 3> own_middle{};
};

// Rewrites an object's packets in place (the recorded packets one after the other); returns whether it changed them.
// Changed modes and texture coordinates go to the packets' faces.
using ItemPacketFix =
    std::function<bool(std::span<std::uint8_t> packets, std::uint32_t vertex_count, std::uint32_t packet_count)>;

// Reads object `object` of an item model: `glb` the converted model, `sidecar` its .model.json (may be empty: the
// faces are then the glb's triangles as they are, and no fix applies). `path` only names errors.
Result<ItemMesh> read_item_mesh(std::span<const std::uint8_t> glb, std::span<const std::uint8_t> sidecar, int object,
                                const ItemPacketFix* fix, std::string_view path);

// The item model files of game file `model` ("ITEM/IT_001.TMD"): "convert/models/ITEM/IT_001.glb" and
// "convert/models/ITEM/IT_001.model.json". A mod's own model is named by its glb below convert/models/
// ("cybil_story/RADIO.glb" in an item's "close_up_model"): a glb with its own embedded picture, any node, metres,
// +Y up, front +Z; it is scaled to the size of the game's item models (the mean of its two largest sides 440 units).
std::string item_glb_path(std::string_view model);
std::string item_sidecar_path(std::string_view model);

// Object `object` of game file `model` through the scene's file reader, read once per (model, object, fix name);
// null when it cannot be read (logged once). `fix` may be empty.
std::shared_ptr<const ItemMesh> find_item_mesh(std::string_view model, int object, std::string_view fix_name,
                                               const ItemPacketFix& fix);

// Model to view: Q12 rotation rows and the translation (view units).
struct ItemModelPose {
    std::int16_t m[3][3] = {};
    std::int32_t t[3] = {};
};

struct ItemProjection {
    float h = 0;      // projection distance (screen units)
    float ofs_x = 0;  // projection centre (screen units)
    float ofs_y = 0;
};

// The sheet of a material the import left unresolved; nullopt: the model is not drawn.
using ItemSheetResolver = std::function<std::optional<ItemSheet>(std::uint16_t tpage, std::uint16_t clut)>;

// A TIM picture of the item sheets as the scene samples it: the colours of CLUT row `palette` the way the PS1 path
// read a 5-bit channel (c * 8 / 256 of full), alpha 0 for the transparent word 0, 128 for words with the STP bit, 255
// otherwise. `indexed_png` is the import's indexed picture, `tim_json` its description.
Result<std::vector<std::uint8_t>> item_sheet_rgba(std::span<const std::uint8_t> indexed_png,
                                                  std::span<const std::uint8_t> tim_json, int palette, int& width,
                                                  int& height);

struct ItemModelDraw {
    bool drawn = false;        // false: the model is not drawable or a texture is missing; nothing is drawn
    std::vector<int> meshes;   // indices into current_frame().prelit, in draw order
    int bucket = 0;            // OT bucket of the PS1 drawer: mean sort-depth bucket of the drawn triangles
};

// Adds the model to the current frame. `dim` is the carousel dim (256 = none), `shift` the OT sort shift and
// `bucket_count` the size of the ordering table the entry goes to (the bucket is clamped to 1 .. count - 1).
ItemModelDraw add_item_model(const ItemMesh& model, const ItemModelPose& pose, const ItemProjection& projection,
                             int dim, int shift, int bucket_count, const ItemSheetResolver& resolve);

}  // namespace sh::scene
