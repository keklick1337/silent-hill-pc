#pragma once

// The streamed window of map cells: which cell files sit in which slot, when a
// slot counts as loaded, the map's texture pool and which cells hold pages in it. Engine types only. No cell or
// library bytes are read: a slot's read is a paced request in the file queue (the same steps as the disc read it
// stands for, so every ticket completes on the same update as the original's), and its material list (texture names in
// file order) comes from the area file (maps/areas/<AREA>.glb, extras "materials"). The textures themselves are
// still read into the pool's places: the effects find them there by name.
//
// The game side is the host (CellStreamHost); src/engine/runtime/cell_stream_runtime.cpp connects the running game.

#include "engine/world/cell_grid.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::world {

inline constexpr std::int32_t kNoTicket = -1;
inline constexpr std::int32_t kNoFile = -1;

// A texture name as the game compares it: 8 bytes, NUL padded.
using TextureName = std::array<char, 8>;
TextureName texture_name(std::string_view name);
std::string_view texture_name_view(const TextureName& name);
// A texture that fills half a page: the last non-NUL character of its name is 'H'.
bool is_half_page(const TextureName& name);

// Where a pool texture goes in video memory (layout of the file queue's image place).
struct TexturePlace {
    std::uint8_t page[2] = {0, 0};
    std::uint8_t u = 0;
    std::uint8_t v = 0;
    std::int16_t palette_x = 0;
    std::int16_t palette_y = 0;
};

struct PoolTexture {
    TextureName name{};
    std::int32_t ref_count = 0;
    std::int32_t ticket = kNoTicket;
    TexturePlace place;
};

// The pool's first palette row of the virtual slots (no video memory has such a row: a picture read to a virtual
// slot is uploaded nowhere), and rows per slot group.
inline constexpr std::int16_t kVirtualPaletteRow = 512;
inline constexpr std::int16_t kVirtualSlotRows = 16;

// The texture pool of a map: 8 full pages and 2 half pages of video memory, and with resident textures 192 + 64
// virtual slots after them (the last half one is the bullet decals' and is never offered).
class TexturePool {
public:
    static constexpr std::size_t kFullPages = 8;
    static constexpr std::size_t kHalfPages = 2;
    static constexpr std::size_t kFullVirtual = 192;
    static constexpr std::size_t kHalfVirtual = 64;
    static constexpr std::size_t kDecalSlot = 255;  // virtual slot id kept for the decals

    enum class List : std::uint8_t { Full, Half };

    void init(bool resident);
    // Forgets the textures nothing references (`resident`: the virtual slots too).
    void clear_unreferenced(bool resident);

    std::size_t listed(List list) const { return list == List::Full ? full_.size() : half_.size(); }
    PoolTexture* entry(List list, std::size_t i) { return list == List::Full ? full_[i] : half_[i]; }
    const PoolTexture* entry(List list, std::size_t i) const { return list == List::Full ? full_[i] : half_[i]; }

    // The texture with that name that has a read (full pages first): what the effects look up.
    const PoolTexture* find(const TextureName& name) const;

    static bool is_virtual(const PoolTexture& t) { return t.place.palette_y >= kVirtualPaletteRow; }

private:
    std::array<PoolTexture, kFullPages + kFullVirtual> full_storage_{};
    std::array<PoolTexture, kHalfPages + kHalfVirtual> half_storage_{};
    std::vector<PoolTexture*> full_;
    std::vector<PoolTexture*> half_;
};

// What the stream asks of the running game.
class CellStreamHost {
public:
    virtual ~CellStreamHost() = default;

    // The file queue. `pace_read` enqueues a read of `file` that transfers nothing, into the buffer the cell or the
    // global library would have been read into (the queue's buffer checks see the same addresses); kNoTicket when
    // the queue does not know the file.
    virtual std::int32_t pace_read(std::int32_t file, bool library) = 0;
    virtual std::int32_t read_texture(std::int32_t file, const TexturePlace& place) = 0;
    virtual bool ticket_loaded(std::int32_t ticket) = 0;
    virtual int queue_length() = 0;
    virtual void queue_update() = 0;

    // The texture file "<NAME>.TIM" from `first` on; kNoFile when there is none.
    virtual std::int32_t texture_file(const TextureName& name, std::int32_t first) = 0;
    // Fills the grid with the cell files of the map tag `tag` from file `first` on.
    virtual void list_cell_files(std::string_view tag, std::int32_t first, CellGrid& grid) = 0;
    // Material lists of a cell file and of a library file; null when the area file has none.
    virtual const std::vector<TextureName>* cell_materials(std::int32_t file) = 0;
    virtual const std::vector<TextureName>* library_materials(std::int32_t file) = 0;

    // The cell in `slot` became ready (its collision can be placed).
    virtual void cell_ready(std::size_t slot, CellCoord cell, std::int32_t grid_file) = 0;
    // The pool was laid out again: the renderer's pool textures and the decals go.
    virtual void pool_reset() = 0;
    virtual void log(const std::string& message) = 0;

    // Settings and live state.
    virtual bool preload_exteriors() = 0;     // preload_chunks: every exterior cell at once
    virtual bool resident_textures() = 0;     // resident_textures: the virtual pool slots
    virtual int draw_distance_pct() = 0;
    virtual bool free_camera() = 0;
    virtual bool modern_culling() = 0;        // culling != 0
    virtual bool whole_map_enabled() = 0;     // whole_map_exteriors and its fog / precision conditions
    virtual CellCoord player_cell() = 0;
    virtual bool exact_cell_arena() = 0;      // a boss arena: interiors draw only the player's cell
    virtual bool has_rooms() = 0;
    virtual std::uint8_t room_at(std::int32_t x, std::int32_t z) = 0;
    // Whether the scene library of slot index `library` (2 the global library, 3 + slot a cell) has the model.
    virtual bool library_has_object(int library, std::string_view name) = 0;
};

// Load states of a slot or library (ordered: callers compare against Loaded).
enum class LoadState : std::uint8_t {
    Invalid = 0,    // no request
    Unloaded = 1,   // the read has not completed
    Corrupted = 2,  // read, but not prepared or a texture is not in yet
    Loaded = 3,
};

class CellStream {
public:
    static constexpr std::size_t kSlots = 256;

    struct Material {
        TextureName name{};
        bool preset = false;            // given its image by the game (the sewer water), never claims a texture
        PoolTexture* texture = nullptr;
    };

    struct Slot {
        CellCoord cell;
        std::int32_t file = kNoFile;
        std::int32_t ticket = kNoTicket;
        std::int32_t padded0 = 0;  // padded distance (Q8) of the first sample point
        std::int32_t padded1 = 0;  // of the second
        std::int32_t material_count = 0;  // half-page materials, 0 while not ready
        std::uint32_t outside_count = 0;
        bool ready = false;        // read and prepared
        std::vector<Material> materials;
    };

    struct Library {
        std::int32_t file = kNoFile;
        std::int32_t ticket = 0;
        bool ready = true;
        std::vector<Material> materials;
    };

    // The map is set up from nothing: an empty global library, no slots, a fresh pool.
    void init(CellStreamHost& host);
    // The map's files: its tag, global library (kNoFile keeps the current one), slot count, exterior or not and
    // where its cell and texture files start. Rebuilds the window when the map or the slot count changes.
    void set_map_files(CellStreamHost& host, std::string_view tag, std::int32_t library_file, int slot_count,
                       bool exterior, std::int32_t first_cell_file, std::int32_t first_texture_file);
    // An event puts `file` at the grid cell: slots holding that cell drop it.
    void place_file_at(CellStreamHost& host, std::int32_t file, CellCoord cell);
    // One step of the window around two sample points (Q12): queue reads, prepare what is read, hand out textures.
    void update(CellStreamHost& host, std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1);
    // Drops every slot (their textures too).
    void clear_slots();
    // Drops the global library, every slot, and lays out the pool again.
    void reset(CellStreamHost& host);
    // Forgets the pool textures nothing references.
    void clear_unreferenced_textures(CellStreamHost& host);

    // Whether the window is ready for play: the global library and every slot the sample points are in.
    bool window_loaded(CellStreamHost& host);
    LoadState slot_state(CellStreamHost& host, std::size_t slot);
    LoadState library_state(CellStreamHost& host);
    // The global library's read completed and it is prepared (or no read is pending at all).
    bool library_ready(CellStreamHost& host);

    // The library a world object's model is in (2 the global library, 3 + slot a cell, 0 none), searched like the
    // game does: the global library, then the cells around the point (Q12) nearest first.
    int object_library(CellStreamHost& host, std::string_view name, std::int32_t x, std::int32_t z);
    // A point (Q12) near the window: exteriors within 4.5 m of the cell, interiors in the window's cell.
    bool near_window(std::int32_t x, std::int32_t z) const;

    // Whether the slot's cell is drawn: whole-town mode and exteriors draw all, interiors the player's cell and
    // the cells of the same room around it.
    bool cell_drawn(CellStreamHost& host, std::size_t slot);
    // Whole-town mode: exterior, preloaded, resident textures, and the player in an outdoor cell.
    bool whole_map_active(CellStreamHost& host);
    // A map event placed a cell (the whole-town classification ignores it); cleared at map load.
    void clear_parked_cells();

    // Collision: the slots that answer for cells. `ready` per slot.
    std::vector<ResidentCell>& resident_cells(CellStreamHost& host);

    // The pool texture of that name with a read (effects).
    const PoolTexture* texture(const TextureName& name) const { return pool_.find(name); }

    // State.
    bool exterior() const { return exterior_; }
    int slot_count() const { return slot_count_; }
    const Slot& slot(std::size_t i) const { return slots_[i]; }
    const Library& library() const { return library_; }
    CellCoord window_cell() const { return window_cell_; }
    std::int32_t position_x() const { return position_x_; }
    std::int32_t position_z() const { return position_z_; }
    const TexturePool& pool() const { return pool_; }

    // One line of the window's state (the A/B comparison of the stream).
    std::string describe(CellStreamHost& host);

private:
    LoadState ticket_state(CellStreamHost& host, std::int32_t ticket) const;
    bool slot_loaded(CellStreamHost& host, const Slot& s) const;
    bool textures_loaded(CellStreamHost& host, const std::vector<Material>& materials) const;
    PoolTexture* claim(CellStreamHost& host, const Material& m, TexturePool::List list, std::size_t limit);
    void load_materials(CellStreamHost& host, std::vector<Material>& materials, TexturePool::List list,
                        std::optional<bool> half_filter, std::size_t limit);
    void load_cell_materials(CellStreamHost& host, Slot& s);
    static void release(std::vector<Material>& materials);
    void prepare_library(CellStreamHost& host);
    void prepare_slot(CellStreamHost& host, std::size_t index);
    std::int32_t start_read(CellStreamHost& host, Slot& s, std::int32_t file, CellCoord cell, std::int32_t x0,
                            std::int32_t z0, std::int32_t x1, std::int32_t z1);
    void sample(CellStreamHost& host, std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1);
    void apply_materials(CellStreamHost& host);
    void stream_window(CellStreamHost& host, std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1);
    void preload_all(CellStreamHost& host, std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1);
    void drain(CellStreamHost& host);
    bool present(CellCoord cell) const;
    bool outdoor(CellStreamHost& host, CellCoord cell);

    std::array<Slot, kSlots> slots_{};
    int slot_count_ = 0;
    bool exterior_ = true;
    std::string tag_;
    std::int32_t first_cell_file_ = 0;
    std::int32_t first_texture_file_ = 0;
    Library library_;
    TexturePool pool_;
    std::int32_t position_x_ = 0;
    std::int32_t position_z_ = 0;
    CellCoord window_cell_;

    std::vector<CellCoord> parked_;
    OutdoorCells outdoor_;
    bool outdoor_valid_ = false;
    std::vector<ResidentCell> resident_;
};

// The running game's stream.
CellStream& cell_stream();

}  // namespace sh::world
