// The running game's window of map cells: the engine stream (src/engine/world/cell_stream.h) on the file queue, the
// file table, the area files, the settings and the scene library.

#include "engine/runtime/cell_stream_runtime.h"

#include "engine/app/loader/game_files.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/memory/work_buffers.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_runtime.h"
#include "engine/world/cell_stream.h"
#include "engine/world/game/character_world.h"
#include "engine/world/world.h"
#include "engine/world/world_atmosphere.h"
#include "render/decals/pc_decals.h"
#include "render/gpu/render_state.h"
#include "render/world/world_scene.h"
#include "render/world/world_scene_source.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>

extern int g_DebugCamEnabled;


namespace sh::world::stream {

namespace {

// Cell reads go to a buffer of the stream's own (never touched: the reads transfer nothing).
constexpr std::size_t kCellBufferBytes = 1u << 20;
// The texture file type of the file table.
constexpr int kTextureType = 0;
// The cell file type.
constexpr int kCellType = 6;

std::string file_stem(std::int32_t file) {
    std::string name = sh::loader::disc_files().name(file);
    if (const std::size_t dot = name.find('.'); dot != std::string::npos) {
        name.resize(dot);
    }
    return name;
}

class GameHost final : public CellStreamHost {
public:
    std::int32_t pace_read(std::int32_t file, bool library) override {
        void* dest = library ? sh::memory::work_buffers().data(sh::memory::WorkBuffer::LibraryModels) : cell_buffer();
        return sh::loader::queue_pace(file, dest);
    }
    std::int32_t read_texture(std::int32_t file, const TexturePlace& place) override {
        sh::loader::ImagePlace p;
        static_assert(sizeof(p) == sizeof(place));
        std::memcpy(&p, &place, sizeof(p));
        return sh::loader::queue_read_image(file, sh::memory::work_buffers().data(sh::memory::WorkBuffer::MapTexture), &p);
    }
    bool ticket_loaded(std::int32_t ticket) override { return sh::loader::queue_loaded(ticket); }
    int queue_length() override { return sh::loader::queue_length(); }
    void queue_update() override { sh::loader::queue_update(); }

    std::int32_t texture_file(const TextureName& name, std::int32_t first) override {
        char file[16] = {};
        std::memcpy(file, name.data(), name.size());
        std::strcat(file, ".TIM");
        return sh::loader::disc_files().next_named(file, kTextureType, first);
    }
    void list_cell_files(std::string_view tag, std::int32_t first, CellGrid& grid) override {
        const sh::loader::DiscFiles& files = sh::loader::disc_files();
        for (int i = first; i < files.count(); ++i) {
            if (files.type(i) == kCellType) {
                grid.add_file(tag, files.name(i), i);
            }
        }
    }
    const std::vector<TextureName>* cell_materials(std::int32_t file) override { return materials(file); }
    const std::vector<TextureName>* library_materials(std::int32_t file) override { return materials(file); }

    void cell_ready(std::size_t slot, CellCoord cell, std::int32_t grid_file) override {
        sh::world::game::place_cell_file(static_cast<int>(slot), cell.x, cell.z, grid_file);
    }
    void pool_reset() override {
        // The decals of the map that is left go; their sheet is put again on the next draw.
        Pc_DecalsReset();
    }
    void log(const std::string& message) override { SH_DBG("%s", message.c_str()); }

    bool preload_exteriors() override { return g_PcConfig.preloadChunks != 0; }
    bool resident_textures() override { return g_PcConfig.residentTextures != 0; }
    int draw_distance_pct() override { return g_PcConfig.drawDistancePct; }
    bool free_camera() override { return g_DebugCamEnabled != 0; }
    bool modern_culling() override { return g_PcConfig.culling != 0; }
    bool whole_map_enabled() override {
        // The far band sorts on precise per-vertex depth: without it the mode stays off. Setting 2 also skips the
        // dark exteriors (black fog), where nothing far off is lit enough to see.
        if (!sh::render::precise().enabled) {
            return false;
        }
        if (g_PcConfig.wholeMapExteriors == 2) {
            const Color8 fog = atmosphere().state().fog.color;
            return sh::render::fog().strength < 1.0f && (fog.r >= 40 || fog.g >= 40 || fog.b >= 40);
        }
        return g_PcConfig.wholeMapExteriors != 0;
    }
    CellCoord player_cell() override {
        const sh::entities::Vec3 p = sh::game_state::chara_position(0);
        return cell_of(p.x, p.z);
    }
    bool exact_cell_arena() override {
        const int map = sh::game_state::save_map_index();
        return map == sh::game_state::map_idx::kMap1_S05 || map == sh::game_state::map_idx::kMap7_S03;
    }
    bool has_rooms() override { return sh::maps::map_runtime().has_rooms(); }
    std::uint8_t room_at(std::int32_t x, std::int32_t z) override { return sh::maps::map_runtime().room_at(x, z); }
    bool library_has_object(int library, std::string_view name) override {
        return sh::world_scene::has_object(library, name);
    }

private:
    static void* cell_buffer() {
        static const std::unique_ptr<std::uint8_t[]> buffer(new std::uint8_t[kCellBufferBytes]);
        return buffer.get();
    }
    const std::vector<TextureName>* materials(std::int32_t file) {
        sh::world::game::boot_world();
        if (!sh::loader::disc_files().valid(file)) {
            return nullptr;
        }
        return sh::world::world().materials(file_stem(file));
    }
};

GameHost& host() {
    static GameHost h;
    return h;
}

}  // namespace

void map_init() { cell_stream().init(host()); }
void clear_parked_cells() { cell_stream().clear_parked_cells(); }
void set_map_files(const char* tag, std::int32_t library_file, int slot_count, bool exterior) {
    cell_stream().set_map_files(host(), tag, library_file, slot_count, exterior, 0, 0);
}
void place_file_at(std::int32_t file, std::int32_t cell_x, std::int32_t cell_z) {
    cell_stream().place_file_at(host(), file, {cell_x, cell_z});
}
void update(std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1) {
    cell_stream().update(host(), x0, z0, x1, z1);
    static const bool trace = std::getenv("SH_CELL_STREAM_TRACE") != nullptr;
    if (trace) {
        SH_LOG("[CELLSTREAM] t=%d %s", sh::game_state::tick_count(), cell_stream().describe(host()).c_str());
    }
}
void reset() { cell_stream().reset(host()); }
void clear_slots() { cell_stream().clear_slots(); }
void clear_unreferenced_textures() { cell_stream().clear_unreferenced_textures(host()); }

bool window_loaded() { return cell_stream().window_loaded(host()); }
bool slot_loaded(int slot) {
    return slot >= 0 && slot < cell_stream().slot_count() &&
           cell_stream().slot_state(host(), static_cast<std::size_t>(slot)) >= LoadState::Loaded;
}
bool library_ready() { return cell_stream().library_ready(host()); }
bool exterior() { return cell_stream().exterior(); }
int slot_count() { return cell_stream().slot_count(); }
bool near_window(std::int32_t x, std::int32_t z) { return cell_stream().near_window(x, z); }
int object_library(const char* name8, std::int32_t x, std::int32_t z) {
    const std::string_view name(name8, name8 != nullptr ? strnlen(name8, 8) : 0);
    return cell_stream().object_library(host(), name, x, z);
}

void drawn_cells(std::vector<DrawnCell>& out) {
    CellStream& s = cell_stream();
    for (int i = 0; i < s.slot_count(); ++i) {
        const std::size_t slot = static_cast<std::size_t>(i);
        if (s.slot_state(host(), slot) < LoadState::Loaded || !s.cell_drawn(host(), slot)) {
            continue;
        }
        out.push_back({i, *slot_stem(i), s.slot(slot).cell.x, s.slot(slot).cell.z});
    }
}

std::optional<std::string> slot_stem(int slot) {
    CellStream& s = cell_stream();
    if (slot < 0 || slot >= s.slot_count()) {
        return std::nullopt;
    }
    // The world module's name when it placed the cell, otherwise the file the grid names for the slot's cell.
    if (const CellCollision* cell = world().placed(static_cast<std::size_t>(slot))) {
        return cell->name;
    }
    const std::int32_t file = map_cells().file_at(s.slot(static_cast<std::size_t>(slot)).cell);
    return file == kNoFile ? std::string() : file_stem(file);
}

std::string library_stem() { return file_stem(cell_stream().library().file); }

bool slot_cell(int slot, std::int32_t& cell_x, std::int32_t& cell_z) {
    if (slot < 0 || slot >= cell_stream().slot_count()) {
        return false;
    }
    const CellCoord c = cell_stream().slot(static_cast<std::size_t>(slot)).cell;
    cell_x = c.x;
    cell_z = c.z;
    return true;
}

bool texture_place(const std::string& name, std::uint16_t& page, std::uint16_t& palette_x, std::uint16_t& palette_y) {
    const PoolTexture* t = cell_stream().texture(texture_name(name));
    if (t == nullptr) {
        return false;
    }
    page = t->place.page[1];
    palette_x = static_cast<std::uint16_t>(t->place.palette_x);
    palette_y = static_cast<std::uint16_t>(t->place.palette_y);
    return true;
}

std::int32_t collision_slot(std::int32_t x, std::int32_t z) {
    const CollisionCellLookup found = collision_cell_at(cell_stream().resident_cells(host()), map_cells(), x, z);
    switch (found.kind) {
        case CollisionCellLookup::Kind::Resident:
            return static_cast<std::int32_t>(found.slot);
        case CollisionCellLookup::Kind::Open:
            return kOpen;
        case CollisionCellLookup::Kind::Missing:
            break;
    }
    // A cell file is there but no slot serves it: the miss reads as ground far below and movement refuses the step.
    static int s_last = -1000;
    if (sh::game_state::tick_count() - s_last > 30) {
        s_last = sh::game_state::tick_count();
        const CellCoord cell = cell_of(x, z);
        SH_DBG("[COLL-MISS] cell(%d,%d) pos(%.1f,%.1f) slots=%d", cell.x, cell.z, x / 4096.0, z / 4096.0,
               cell_stream().slot_count());
    }
    return kMissing;
}

}  // namespace sh::world::stream

// Console `COLLSCOPE 0/1`: with the whole exterior preloaded, only the 3 x 3 cells around the window centre collide
// (default 1), as on the console.
int g_PcChunkCollisionLocalScope = 1;

namespace sh::world::stream {

int collision_slots(std::int32_t* slots, int capacity) {
    CellStream& s = cell_stream();
    const bool whole = g_PcChunkCollisionLocalScope && g_PcConfig.preloadChunks && s.exterior();
    const std::vector<ResidentCell>& cells = s.resident_cells(host());
    int count = 0;
    for (std::size_t i = 0; i < cells.size() && count < capacity; ++i) {
        if (cells[i].ready && in_collision_scope(cells[i].cell, s.window_cell(), whole)) {
            slots[count++] = static_cast<std::int32_t>(i);
        }
    }
    return count;
}

}  // namespace sh::world::stream

// ---- PC view helpers the engine reads by name (engine/config/pc_config.h) ------------------------------------------

extern "C" {

int Pc_WholeMapDrawActive(void) {
    return sh::world::cell_stream().whole_map_active(sh::world::stream::host()) ? 1 : 0;
}

// Outdoors only: thin fog shows the spawn ring on the open streets; interiors keep the console's spawn rules.
int Pc_LongViewActive(void) { return sh::world::cell_stream().exterior() && sh::render::fog().strength < 1.0f ? 1 : 0; }

// Whether a world point (Q12) is inside the view cone with a 3 m margin: a spawning monster is a body, not a point.
int Pc_WorldPointInView(int x, int z) {
    const sh::world_scene::source::Camera cam = sh::world_scene::source::camera();
    const std::int32_t margin = (3 * 4096) >> 4;
    const std::int32_t px = x >> 4;
    const std::int32_t pz = z >> 4;
    const std::int32_t vx = static_cast<std::int32_t>(
                                (static_cast<std::int64_t>(cam.m[0][0]) * px + static_cast<std::int64_t>(cam.m[0][2]) * pz) >> 12) +
                            cam.t[0];
    const std::int32_t vz = static_cast<std::int32_t>(
                                (static_cast<std::int64_t>(cam.m[2][0]) * px + static_cast<std::int64_t>(cam.m[2][2]) * pz) >> 12) +
                            cam.t[2];
    if (vz < -margin) {
        return 0;
    }
    // The cone's half tangent from the projection (160 / H at 4:3), widened by the window's aspect and 30%: never
    // tighter than what is on screen.
    const float h = cam.h < 1.0f ? 1.0f : cam.h;
    const float aspect = PcConfig_RenderHeight() > 0
                             ? static_cast<float>(PcConfig_RenderWidth()) / static_cast<float>(PcConfig_RenderHeight())
                             : 4.0f / 3.0f;
    float slope = (160.0f / h) * (aspect / (4.0f / 3.0f)) * 1.3f;
    if (slope < 1.0f) {
        slope = 1.0f;
    }
    const float ax = static_cast<float>(vx < 0 ? -vx : vx);
    return ax <= static_cast<float>(vz) * slope + static_cast<float>(margin) ? 1 : 0;
}

}  // extern "C"

namespace sh::world::stream {

void pool_write_probe(int x, int y, int w, int h) {
    // Diagnostic: an image upload that lands on a page a cell still references (2D screens and boss effects write
    // straight into the pool's pages) -- such a page stays garbled while it is pinned.
    static int s_logged = 0;
    if (s_logged >= 64) {
        return;
    }
    const TexturePool& pool = cell_stream().pool();
    for (std::size_t i = 0; i < TexturePool::kFullPages + TexturePool::kHalfPages; ++i) {
        const bool full = i < TexturePool::kFullPages;
        const TexturePool::List list = full ? TexturePool::List::Full : TexturePool::List::Half;
        const std::size_t at = full ? i : i - TexturePool::kFullPages;
        if (at >= pool.listed(list)) {
            continue;  // before the map's pool is laid out
        }
        const PoolTexture* t = pool.entry(list, at);
        const int sx = t->place.u + ((t->place.page[1] & 0xF) << 6);
        const int sy = t->place.v + ((t->place.page[1] << 4) & 0x100);
        const int sw = full ? 64 : 32;
        if (t->ref_count <= 0 || x >= sx + sw || x + w <= sx || y >= sy + 256 || y + h <= sy) {
            continue;
        }
        SH_DBG("[POOLSTOMP] upload (%d,%d %dx%d) hits pinned pool page %d '%.8s' (%d,%d)", x, y, w, h,
               static_cast<int>(i), t->name.data(), sx, sy);
        ++s_logged;
    }
}

}  // namespace sh::world::stream

// ---- The fog floor (scenes made for the stock fog) -----------------------------------------------------------------

namespace {
bool s_fog_floor_asked = false;
bool s_fog_floor_held = false;
float s_fog_floor_saved = 0.0f;
}  // namespace

extern "C" {

// A scene made for the stock fog (the final map's ending, where thin fog shows what is behind the shot) raises the
// live fog to at least the default for as long as it asks, every frame; the user's value comes back the first frame
// nobody asks.
void Pc_FogFloorRequest(void) {
    if (!s_fog_floor_held) {
        s_fog_floor_held = true;
        s_fog_floor_saved = sh::render::fog().strength;
    }
    if (sh::render::fog().strength < 1.0f) {
        sh::render::fog().strength = 1.0f;
    }
    s_fog_floor_asked = true;
}

void Pc_FogFloorFrameEnd(void) {
    if (s_fog_floor_held && !s_fog_floor_asked) {
        sh::render::fog().strength = s_fog_floor_saved;
        s_fog_floor_held = false;
    }
    s_fog_floor_asked = false;
}

}  // extern "C"
