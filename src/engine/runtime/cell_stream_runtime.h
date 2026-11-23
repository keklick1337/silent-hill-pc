#pragma once

// The running game's window of map cells (src/engine/world/cell_stream.h): the
// stream over the file queue, the file table, the area files and the settings. Positions are Q12.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sh::world::stream {

// Map set-up and the window.
void map_init();
void clear_parked_cells();
void set_map_files(const char* tag, std::int32_t library_file, int slot_count, bool exterior);
void place_file_at(std::int32_t file, std::int32_t cell_x, std::int32_t cell_z);
void update(std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1);
void reset();
void clear_slots();
void clear_unreferenced_textures();

// States.
bool window_loaded();
bool slot_loaded(int slot);  // read, prepared and its textures in
bool library_ready();
bool exterior();
int slot_count();
bool near_window(std::int32_t x, std::int32_t z);
// The library of a world object's model (2 the global library, 3 + slot a cell, 0 none).
int object_library(const char* name8, std::int32_t x, std::int32_t z);

// The cells to draw (loaded and in the draw window), with their file stems.
struct DrawnCell {
    int slot = 0;
    std::string stem;
    std::int32_t cell_x = 0;
    std::int32_t cell_z = 0;
};
void drawn_cells(std::vector<DrawnCell>& out);
std::optional<std::string> slot_stem(int slot);
std::string library_stem();
// Grid cell of the cell in `slot`.
bool slot_cell(int slot, std::int32_t& cell_x, std::int32_t& cell_z);

// The pool texture `name` with a read: its texture page row and palette position.
bool texture_place(const std::string& name, std::uint16_t& page, std::uint16_t& palette_x, std::uint16_t& palette_y);

// Collision: the slot answering at a point, kOpen (no cell file there) or kMissing (a file but no ready slot).
inline constexpr std::int32_t kOpen = -1;
inline constexpr std::int32_t kMissing = -2;
std::int32_t collision_slot(std::int32_t x, std::int32_t z);
int collision_slots(std::int32_t* slots, int capacity);

// Diagnostic of an image upload over a pinned pool page.
void pool_write_probe(int x, int y, int w, int h);

}  // namespace sh::world::stream
