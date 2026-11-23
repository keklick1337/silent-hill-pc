// SPDX-License-Identifier: GPL-3.0-or-later

#include "framework/sh_test.h"

#include "engine/world/cell_stream.h"

#include <map>
#include <string>
#include <vector>

using namespace sh::world;

namespace {

// A queue that completes one request per update, in order; files by name.
class FakeHost final : public CellStreamHost {
public:
    std::int32_t next = 0;
    std::int32_t done = 0;
    std::vector<std::int32_t> texture_reads;  // files
    std::map<std::int32_t, std::vector<TextureName>> cells;
    std::map<std::int32_t, std::vector<TextureName>> libraries;
    std::map<std::string, std::int32_t> textures;
    std::vector<std::pair<std::size_t, CellCoord>> ready;
    std::vector<std::pair<std::string, std::int32_t>> grid_files;
    bool preload = false;
    bool resident = false;

    std::int32_t pace_read(std::int32_t, bool) override { return next++; }
    std::int32_t read_texture(std::int32_t file, const TexturePlace&) override {
        texture_reads.push_back(file);
        return next++;
    }
    bool ticket_loaded(std::int32_t ticket) override { return ticket < done; }
    int queue_length() override { return next - done; }
    void queue_update() override {
        if (done < next) ++done;
    }
    std::int32_t texture_file(const TextureName& name, std::int32_t) override {
        auto it = textures.find(std::string(texture_name_view(name)));
        return it == textures.end() ? kNoFile : it->second;
    }
    void list_cell_files(std::string_view tag, std::int32_t, CellGrid& grid) override {
        for (const auto& [name, file] : grid_files) grid.add_file(tag, name, file);
    }
    const std::vector<TextureName>* cell_materials(std::int32_t file) override {
        auto it = cells.find(file);
        return it == cells.end() ? nullptr : &it->second;
    }
    const std::vector<TextureName>* library_materials(std::int32_t file) override {
        auto it = libraries.find(file);
        return it == libraries.end() ? nullptr : &it->second;
    }
    void cell_ready(std::size_t slot, CellCoord cell, std::int32_t) override { ready.push_back({slot, cell}); }
    void pool_reset() override {}
    void log(const std::string&) override {}
    bool preload_exteriors() override { return preload; }
    bool resident_textures() override { return resident; }
    int draw_distance_pct() override { return 100; }
    bool free_camera() override { return false; }
    bool modern_culling() override { return true; }
    bool whole_map_enabled() override { return false; }
    CellCoord player_cell() override { return {0, 0}; }
    bool exact_cell_arena() override { return false; }
    bool has_rooms() override { return false; }
    std::uint8_t room_at(std::int32_t, std::int32_t) override { return 0; }
    bool library_has_object(int, std::string_view) override { return false; }

    void drain() {
        while (done < next) ++done;
    }
};

std::vector<TextureName> names(std::initializer_list<const char*> list) {
    std::vector<TextureName> out;
    for (const char* n : list) out.push_back(texture_name(n));
    return out;
}

// The middle of cell (x, z), Q12.
std::int32_t mid(std::int32_t c) { return c * kCellSize + kCellSize / 2; }

}  // namespace

SH_TEST(cell_stream_names) {
    SH_CHECK(texture_name("ABC") == (TextureName{'A', 'B', 'C', 0, 0, 0, 0, 0}));
    SH_CHECK(texture_name("ABCDEFGHIJ") == (TextureName{'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'}));
    SH_CHECK(texture_name_view(texture_name("WALL")) == "WALL");
    SH_CHECK(is_half_page(texture_name("FLOORH")));
    SH_CHECK(!is_half_page(texture_name("FLOOR")));
    SH_CHECK(!is_half_page(TextureName{}));
}

SH_TEST(cell_stream_pool_layout) {
    TexturePool pool;
    pool.init(false);
    SH_CHECK(pool.listed(TexturePool::List::Full) == 8);
    SH_CHECK(pool.listed(TexturePool::List::Half) == 2);
    const std::uint8_t rows[8] = {8, 9, 10, 21, 22, 23, 24, 25};
    for (std::size_t i = 0; i < 8; ++i) {
        const PoolTexture* t = pool.entry(TexturePool::List::Full, i);
        SH_CHECK(t->place.page[1] == rows[i]);
        SH_CHECK(t->place.palette_x == static_cast<std::int16_t>(16 * i));
        SH_CHECK(t->place.palette_y == 0);
        SH_CHECK(t->ticket == kNoTicket);
    }
    SH_CHECK(pool.entry(TexturePool::List::Half, 0)->place.u == 0);
    SH_CHECK(pool.entry(TexturePool::List::Half, 1)->place.u == 32);
    SH_CHECK(pool.entry(TexturePool::List::Half, 1)->place.palette_x == 144);
    pool.init(true);
    SH_CHECK(pool.listed(TexturePool::List::Full) == 200);
    SH_CHECK(pool.listed(TexturePool::List::Half) == 65);  // the decal slot is not offered
    const PoolTexture* v = pool.entry(TexturePool::List::Full, 8 + 65);
    SH_CHECK(TexturePool::is_virtual(*v));
    SH_CHECK(v->place.palette_x == 16 && v->place.palette_y == kVirtualPaletteRow + kVirtualSlotRows);
}

SH_TEST(cell_stream_interior_window) {
    FakeHost host;
    host.grid_files = {{"HP0000.IPD", 100}, {"HP0100.IPD", 101}, {"HP0500.IPD", 105}};
    host.cells[100] = names({"WALL", "FLOORH", "DWAVE"});
    host.cells[101] = names({"WALL", "DOOR"});
    host.cells[105] = names({"FAR"});
    host.textures = {{"WALL", 500}, {"FLOORH", 501}, {"DOOR", 502}, {"FAR", 503}};
    CellStream s;
    s.init(host);
    s.set_map_files(host, "HP", kNoFile, 16, false, 0, 0);
    SH_CHECK(s.slot_count() == 16);
    SH_CHECK(map_cells().file_at({1, 0}) == 101);

    s.update(host, mid(0), mid(0), mid(0), mid(0));
    // Two cells of the +-2 window have files; the far one is outside it.
    SH_CHECK(host.next == 2);
    SH_CHECK(!s.window_loaded(host));
    SH_CHECK(s.slot_state(host, 0) == LoadState::Unloaded);
    host.drain();
    s.update(host, mid(0), mid(0), mid(0), mid(0));
    SH_CHECK(host.ready.size() == 2);
    // Both prepared: the textures are read (the shared WALL once), the sewer water's material never claims one.
    SH_CHECK(host.texture_reads.size() == 3);
    SH_CHECK(s.slot_state(host, 0) == LoadState::Corrupted);  // texture reads still pending
    host.drain();
    SH_CHECK(s.slot_state(host, 0) == LoadState::Loaded);
    SH_CHECK(s.window_loaded(host));
    SH_CHECK(s.texture(texture_name("WALL"))->ref_count == 2);
    SH_CHECK(s.texture(texture_name("FLOORH")) == s.pool().entry(TexturePool::List::Half, 0));
    SH_CHECK(s.texture(texture_name("DWAVE")) == nullptr);

    s.update(host, mid(0), mid(0), mid(0), mid(0));
    SH_CHECK(s.slot(0).material_count == 1);  // FLOORH

    // An event swaps the cell under slot 1: the slot drops it and gives its textures back.
    s.place_file_at(host, 105, {1, 0});
    SH_CHECK(s.slot(1).ticket == kNoTicket);
    SH_CHECK(s.texture(texture_name("WALL"))->ref_count == 1);
    SH_CHECK(s.texture(texture_name("DOOR"))->ref_count == 0);
}

SH_TEST(cell_stream_library_pages_and_missing_texture) {
    FakeHost host;
    host.libraries[9] = names({"L1", "L2", "L3", "L4", "L5"});
    host.textures = {{"L1", 1}, {"L2", 2}, {"L3", 3}, {"L4", 4}};
    CellStream s;
    s.init(host);
    SH_CHECK(s.library().ready && s.library().ticket == 0);  // an empty library on ticket 0
    s.set_map_files(host, "XX", 9, 4, true, 0, 0);
    s.update(host, 0, 0, 0, 0);
    SH_CHECK(s.library_state(host) == LoadState::Unloaded);
    SH_CHECK(!s.library_ready(host));
    host.drain();
    s.update(host, 0, 0, 0, 0);
    // The library sees only the first four pages: the fifth material gets none.
    SH_CHECK(s.library().ready);
    SH_CHECK(s.library().materials[3].texture == s.pool().entry(TexturePool::List::Full, 3));
    SH_CHECK(s.library().materials[4].texture == nullptr);
    SH_CHECK(s.library_state(host) == LoadState::Corrupted);
    SH_CHECK(!s.window_loaded(host));

    // A texture with no file claims a page with no read (ticket 0).
    host.libraries[10] = names({"NOFILE"});
    s.set_map_files(host, "XX", 10, 4, true, 0, 0);
    s.update(host, 0, 0, 0, 0);
    host.drain();
    s.update(host, 0, 0, 0, 0);
    SH_CHECK(s.library().materials[0].texture != nullptr);
    SH_CHECK(s.library().materials[0].texture->ticket == 0);
    SH_CHECK(s.library_state(host) == LoadState::Loaded);
}

SH_TEST(cell_stream_reset_and_clear) {
    FakeHost host;
    host.grid_files = {{"HP0000.IPD", 100}};
    host.cells[100] = names({"WALL"});
    host.textures = {{"WALL", 500}};
    CellStream s;
    s.init(host);
    s.set_map_files(host, "HP", kNoFile, 16, false, 0, 0);
    s.update(host, mid(0), mid(0), mid(0), mid(0));
    host.drain();
    s.update(host, mid(0), mid(0), mid(0), mid(0));
    host.drain();
    SH_CHECK(s.texture(texture_name("WALL"))->ref_count == 1);
    s.clear_slots();
    SH_CHECK(s.texture(texture_name("WALL"))->ref_count == 0);
    SH_CHECK(s.texture(texture_name("WALL")) != nullptr);  // still named until the unreferenced ones go
    s.clear_unreferenced_textures(host);
    SH_CHECK(s.texture(texture_name("WALL")) == nullptr);
    SH_CHECK(!s.near_window(mid(3), mid(0)));
    SH_CHECK(s.near_window(mid(0), mid(0)));
}
