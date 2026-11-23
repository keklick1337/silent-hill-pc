#pragma once

// What the world scene (world_scene.h) reads of the running game: the camera of the frame, the map's resident cells
// and their glow sprites, the frame's ordering-table depth scale (world_scene_source.cpp).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::world_scene::source {

// The frame's camera: world to view (rotation rows at unit scale, translation in view units; the unquantised camera
// under smooth_models), the projection distance and centre (screen units) and the far colour lit vertices fade to
// (0..255).
struct Camera {
    double m[3][3] = {};
    double t[3] = {};
    float h = 0;
    float ofs_x = 0;
    float ofs_y = 0;
    std::int32_t far_color[3] = {};
};
Camera camera();

// The map's global model library is loaded, or the map has none: until then no cell is drawn.
bool global_library_ready();

// A resident cell that is loaded and inside the draw window, in slot order. `stem` names its cell file ("ER0402");
// empty when the cell grid does not name one.
struct Cell {
    int slot = 0;
    std::string stem;
    int cell_x = 0;
    int cell_z = 0;
};
void drawable_cells(std::vector<Cell>& out);

// Stem of the cell file in resident slot `slot` (empty: no such file); nullopt when the slot holds no cell.
std::optional<std::string> resident_cell_stem(int slot);
// Stem of the map's global model library file ("THR_GLB").
std::string global_library_stem();

// The current map type's flags (gconfig/map_info.h kMap*) and whether it is the town (THR).
std::uint8_t map_flags();
bool map_is_town();

// The skeleton of character `chara`'s loaded model (the bone-to-part record the skinned drawer reads), nullptr when
// the character has no model registered.
const void* chara_skeleton(int chara);

// Draws the glow sprites of the lamps of the cell in `slot` (effects, not geometry).
void draw_cell_glows(int slot, int shift);

// The depth scale of the frame's ordering-table buckets and the farthest view depth the world draws at `shift`.
void set_bucket_depth_shift(int shift);
std::int32_t world_far_limit(int shift);

}  // namespace sh::world_scene::source
