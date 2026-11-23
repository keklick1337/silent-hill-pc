#include "engine/world/cell_stream.h"

#include <algorithm>
#include <climits>
#include <cstring>

namespace sh::world {

namespace {

// A virtual slot id spread over the palette X bits and 16-row groups.
TexturePlace virtual_place(std::uint8_t page_y, std::size_t id) {
    TexturePlace p;
    p.page[0] = 0;
    p.page[1] = page_y;
    p.palette_x = static_cast<std::int16_t>((id % 64) * 16);
    p.palette_y = static_cast<std::int16_t>(kVirtualPaletteRow + (id / 64) * kVirtualSlotRows);
    return p;
}

// The one texture that must stay in video memory: map4_s03's Twinfeeler animates its palette rows there.
bool needs_video_memory(const TextureName& name) { return name == texture_name("SPUM602F"); }

// The sewer water's material takes the caustic page from the game and never claims a pool texture.
bool preset_material(const TextureName& name) { return name == texture_name("DWAVE"); }

std::vector<CellStream::Material> fresh_materials(const std::vector<TextureName>* names, bool cell) {
    std::vector<CellStream::Material> out;
    if (names == nullptr) {
        return out;
    }
    out.reserve(names->size());
    for (const TextureName& n : *names) {
        out.push_back({n, cell && preset_material(n), nullptr});
    }
    return out;
}

constexpr std::int32_t kLoadsPerUpdate = 9;
constexpr std::int32_t kWideLoadsPerUpdate = 16;
constexpr int kDrainLimit = 500;
constexpr std::size_t kLibraryPages = 4;     // the global library sees only the first full pages
constexpr std::size_t kInteriorTextured = 4;  // interiors without resident textures: the nearest cells keep pages
constexpr std::size_t kClaimsPerUpdate = 3;   // whole-town mode: first-time claims per update, nearest first

}  // namespace

TextureName texture_name(std::string_view name) {
    TextureName n{};
    for (std::size_t i = 0; i < n.size() && i < name.size() && name[i] != '\0'; ++i) {
        n[i] = name[i];
    }
    return n;
}

std::string_view texture_name_view(const TextureName& name) {
    std::size_t len = 0;
    while (len < name.size() && name[len] != '\0') {
        ++len;
    }
    return {name.data(), len};
}

bool is_half_page(const TextureName& name) {
    for (std::size_t i = name.size(); i-- > 0;) {
        if (name[i] != '\0') {
            return name[i] == 'H';
        }
    }
    return false;
}

// ---- TexturePool ------------------------------------------------------------------------------------------------

void TexturePool::init(bool resident) {
    full_.clear();
    half_.clear();
    // Full pages: texture page rows 8, 9, 10, then 21..25; palettes side by side on row 0.
    std::uint8_t y = 8;
    std::int16_t x = 0;
    for (std::size_t i = 0; i < kFullPages; ++i, ++y, x += 16) {
        if (y == 11) {
            y = 21;
        }
        PoolTexture& t = full_storage_[i];
        t = PoolTexture{};
        t.place.page[1] = y;
        t.place.palette_x = x;
        full_.push_back(&t);
    }
    // Half pages: both halves of page row 26.
    for (std::size_t i = 0; i < kHalfPages; ++i, x += 16) {
        PoolTexture& t = half_storage_[i];
        t = PoolTexture{};
        t.place.page[1] = 26;
        t.place.u = static_cast<std::uint8_t>((i & 1) * 32);
        t.place.palette_x = x;
        half_.push_back(&t);
    }
    if (resident) {
        // Listed after the pages: the pages fill first, so the assignment is the console's until it would overflow.
        for (std::size_t k = 0; k < kFullVirtual; ++k) {
            PoolTexture& t = full_storage_[kFullPages + k];
            t = PoolTexture{};
            t.place = virtual_place(8, k);
            full_.push_back(&t);
        }
        for (std::size_t k = 0; k < kHalfVirtual; ++k) {
            const std::size_t id = kFullVirtual + k;
            PoolTexture& t = half_storage_[kHalfPages + k];
            t = PoolTexture{};
            t.place = virtual_place(26, id);
            if (id != kDecalSlot) {
                half_.push_back(&t);
            }
        }
    }
}

void TexturePool::clear_unreferenced(bool resident) {
    const std::size_t full = kFullPages + (resident ? kFullVirtual : 0);
    const std::size_t half = kHalfPages + (resident ? kHalfVirtual : 0);
    auto clear = [](PoolTexture& t) {
        if (t.ref_count == 0) {
            t.name = TextureName{};
            t.ticket = kNoTicket;
        }
    };
    for (std::size_t i = 0; i < full; ++i) {
        clear(full_storage_[i]);
    }
    for (std::size_t i = 0; i < half; ++i) {
        clear(half_storage_[i]);
    }
}

const PoolTexture* TexturePool::find(const TextureName& name) const {
    for (const std::vector<PoolTexture*>* list : {&full_, &half_}) {
        for (const PoolTexture* t : *list) {
            if (t->ticket != kNoTicket && t->name == name) {
                return t;
            }
        }
    }
    return nullptr;
}

// ---- CellStream: materials and the pool ---------------------------------------------------------------------------

LoadState CellStream::ticket_state(CellStreamHost& host, std::int32_t ticket) const {
    if (ticket == kNoTicket) {
        return LoadState::Invalid;
    }
    return host.ticket_loaded(ticket) ? LoadState::Loaded : LoadState::Unloaded;
}

bool CellStream::slot_loaded(CellStreamHost& host, const Slot& s) const {
    return ticket_state(host, s.ticket) == LoadState::Loaded;
}

bool CellStream::textures_loaded(CellStreamHost& host, const std::vector<Material>& materials) const {
    for (const Material& m : materials) {
        if (m.preset) {
            continue;
        }
        if (m.texture == nullptr || !host.ticket_loaded(m.texture->ticket)) {
            return false;
        }
    }
    return true;
}

PoolTexture* CellStream::claim(CellStreamHost& host, const Material& m, TexturePool::List list, std::size_t limit) {
    const std::size_t count = std::min(limit, pool_.listed(list));
    PoolTexture* best_virtual = nullptr;
    PoolTexture* best_page = nullptr;
    std::int32_t best_virtual_ticket = INT_MAX;
    std::int32_t best_page_ticket = INT_MAX;
    for (std::size_t i = 0; i < count; ++i) {
        PoolTexture* t = pool_.entry(list, i);
        if (t->name == m.name) {
            t->ref_count++;
            return t;
        }
        if (t->ref_count != 0) {
            continue;
        }
        // Among the free ones the oldest read goes first (no read at all before any).
        if (TexturePool::is_virtual(*t)) {
            if (t->ticket < best_virtual_ticket) {
                best_virtual_ticket = t->ticket;
                best_virtual = t;
            }
        } else if (t->ticket < best_page_ticket) {
            best_page_ticket = t->ticket;
            best_page = t;
        }
    }
    // A virtual slot is immune to the 2D screens that write straight into the pool's pages; a page is kept for the
    // textures whose palette the game edits in video memory.
    PoolTexture* found = needs_video_memory(m.name) ? best_page : (best_virtual != nullptr ? best_virtual : best_page);
    if (found == nullptr) {
        if (host.resident_textures()) {
            static int s_logged = 0;
            if (s_logged < 8) {
                ++s_logged;
                host.log("[POOLTEX] pool exhausted: no free slot for '" + std::string(texture_name_view(m.name)) + "'");
            }
        }
        return nullptr;
    }
    const std::int32_t file = host.texture_file(m.name, first_texture_file_);
    if (file == kNoFile) {
        // The console read garbage here and kept the slot; the slot is claimed without a read. Only a page has an
        // old picture to show, so a virtual slot gives way to a free page when there is one.
        host.log("[FSQ] chunk TIM '" + std::string(texture_name_view(m.name)) + ".TIM' not in file table -- skipping read");
        if (TexturePool::is_virtual(*found)) {
            std::int32_t best = INT_MAX;
            PoolTexture* page = nullptr;
            for (std::size_t i = 0; i < count; ++i) {
                PoolTexture* t = pool_.entry(list, i);
                if (TexturePool::is_virtual(*t)) {
                    continue;
                }
                if (t->ref_count == 0 && t->ticket < best) {
                    best = t->ticket;
                    page = t;
                }
            }
            if (page != nullptr) {
                found = page;
            }
        }
        found->ticket = 0;
        found->ref_count++;
        found->name = m.name;
        return found;
    }
    found->ticket = host.read_texture(file, found->place);
    found->ref_count++;
    found->name = m.name;
    return found;
}

void CellStream::load_materials(CellStreamHost& host, std::vector<Material>& materials, TexturePool::List list,
                                std::optional<bool> half_filter, std::size_t limit) {
    for (Material& m : materials) {
        if (m.preset || m.texture != nullptr) {
            continue;
        }
        if (half_filter && is_half_page(m.name) != *half_filter) {
            continue;
        }
        m.texture = nullptr;
        m.texture = claim(host, m, list, limit);
    }
}

void CellStream::load_cell_materials(CellStreamHost& host, Slot& s) {
    if (!s.ready) {
        return;
    }
    load_materials(host, s.materials, TexturePool::List::Full, false, SIZE_MAX);
    load_materials(host, s.materials, TexturePool::List::Half, true, SIZE_MAX);
}

void CellStream::release(std::vector<Material>& materials) {
    for (Material& m : materials) {
        if (m.texture != nullptr) {
            m.texture->ref_count = std::max(m.texture->ref_count - 1, 0);
            m.texture = nullptr;
        }
    }
}

// ---- CellStream: map set-up -----------------------------------------------------------------------------------------

void CellStream::init(CellStreamHost& host) {
    for (Slot& s : slots_) {
        s = Slot{};
    }
    slot_count_ = 0;
    exterior_ = true;
    tag_.clear();
    first_cell_file_ = 0;
    first_texture_file_ = 0;
    library_ = Library{};
    position_x_ = position_z_ = 0;
    window_cell_ = {};
    pool_.init(host.resident_textures());
    host.pool_reset();
}

void CellStream::clear_slots() {
    for (Slot& s : slots_) {
        if (s.ticket != kNoTicket && s.ready) {
            release(s.materials);
        }
        s.ticket = kNoTicket;
        s.padded1 = INT_MAX;
        s.outside_count = 0;
    }
}

void CellStream::set_map_files(CellStreamHost& host, std::string_view tag, std::int32_t library_file, int slot_count,
                               bool exterior, std::int32_t first_cell_file, std::int32_t first_texture_file) {
    exterior_ = exterior;
    first_texture_file_ = first_texture_file;
    if (library_file != kNoFile && library_file != library_.file) {
        if (ticket_state(host, library_.ticket) == LoadState::Loaded && library_.ready) {
            release(library_.materials);
        }
        library_.file = library_file;
        library_.ticket = kNoTicket;
    }
    // Preloaded exteriors hold every cell: only another map rebuilds them.
    const bool preload = host.preload_exteriors() && exterior;
    const bool other_map = tag != tag_;
    if ((preload && other_map) || (!preload && (slot_count_ != slot_count || other_map))) {
        clear_slots();
        slot_count_ = preload ? static_cast<int>(kSlots) : std::min(slot_count, static_cast<int>(kSlots));
        first_cell_file_ = first_cell_file;
        tag_ = std::string(tag);
        CellGrid& grid = map_cells();
        grid.clear();
        host.list_cell_files(tag_, first_cell_file_, grid);
    }
}

void CellStream::place_file_at(CellStreamHost& host, std::int32_t file, CellCoord cell) {
    // A placed cell is swapped at run time: whole-town mode never counts it as outdoor.
    if (std::find(parked_.begin(), parked_.end(), cell) == parked_.end()) {
        if (parked_.size() < 16) {
            parked_.push_back(cell);
        }
        outdoor_valid_ = false;
    }
    map_cells().set(cell, file);
    for (int i = 0; i < slot_count_; ++i) {
        Slot& s = slots_[i];
        if (s.cell != cell) {
            continue;
        }
        if (slot_loaded(host, s) && s.ready) {
            release(s.materials);
        }
        s.ticket = kNoTicket;
    }
}

void CellStream::reset(CellStreamHost& host) {
    if (ticket_state(host, library_.ticket) == LoadState::Loaded && library_.ready) {
        release(library_.materials);
    }
    library_ = Library{};
    clear_slots();
    pool_.init(host.resident_textures());
    host.pool_reset();
}

void CellStream::clear_unreferenced_textures(CellStreamHost& host) { pool_.clear_unreferenced(host.resident_textures()); }

void CellStream::clear_parked_cells() {
    parked_.clear();
    outdoor_valid_ = false;
}

// ---- CellStream: the window ----------------------------------------------------------------------------------------

bool CellStream::present(CellCoord cell) const {
    for (int i = 0; i < slot_count_; ++i) {
        if (slots_[i].ticket != kNoTicket && slots_[i].cell == cell) {
            return true;
        }
    }
    return false;
}

std::int32_t CellStream::start_read(CellStreamHost& host, Slot& s, std::int32_t file, CellCoord cell, std::int32_t x0,
                                    std::int32_t z0, std::int32_t x1, std::int32_t z1) {
    if (file == kNoFile) {
        return kNoTicket;
    }
    s.cell = cell;
    s.file = file;
    s.ready = false;
    s.ticket = host.pace_read(file, false);
    s.padded0 = padded_distance_to_cell(x0, z0, cell, exterior_);
    s.padded1 = padded_distance_to_cell(x1, z1, cell, exterior_);
    return s.ticket;
}

void CellStream::sample(CellStreamHost& host, std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1) {
    for (int i = 0; i < slot_count_; ++i) {
        Slot& s = slots_[i];
        if (s.ticket == kNoTicket) {
            s.padded0 = INT_MAX;
            s.padded1 = INT_MAX;
        } else {
            s.padded0 = padded_distance_to_cell(x0, z0, s.cell, exterior_);
            s.padded1 = padded_distance_to_cell(x1, z1, s.cell, exterior_);
        }
        if (!slot_loaded(host, s) || !s.ready) {
            s.material_count = 0;
        } else {
            s.material_count = static_cast<std::int32_t>(
                std::count_if(s.materials.begin(), s.materials.end(), [](const Material& m) { return is_half_page(m.name); }));
        }
        s.outside_count = next_outside_count(s.outside_count, s.padded0, s.padded1);
    }
}

void CellStream::prepare_library(CellStreamHost& host) {
    if (ticket_state(host, library_.ticket) != LoadState::Loaded || library_.ready) {
        return;
    }
    const std::vector<TextureName>* names = host.library_materials(library_.file);
    if (names == nullptr) {
        host.log("[CELLS] no material list for library file " + std::to_string(library_.file));
    }
    library_.materials = fresh_materials(names, false);
    library_.ready = true;
    load_materials(host, library_.materials, TexturePool::List::Full, std::nullopt, kLibraryPages);
}

void CellStream::prepare_slot(CellStreamHost& host, std::size_t index) {
    Slot& s = slots_[index];
    if (!s.ready) {
        const std::vector<TextureName>* names = host.cell_materials(s.file);
        if (names == nullptr) {
            // The cell is not in the area file: like a cell file that does not parse, it never becomes ready.
            return;
        }
        s.materials = fresh_materials(names, true);
        s.ready = true;
        host.cell_ready(index, s.cell, map_cells().file_at(s.cell));
    }
    if (!(host.preload_exteriors() && exterior_)) {
        load_cell_materials(host, s);
    }
}

void CellStream::drain(CellStreamHost& host) {
    for (int n = 0; host.queue_length() > 0 && n < kDrainLimit; ++n) {
        host.queue_update();
    }
}

void CellStream::preload_all(CellStreamHost& host, std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1) {
    drain(host);
    prepare_library(host);
    // Every cell of the grid into the next free slot, each read and prepared at once.
    std::size_t next = 0;
    for (std::int32_t z = CellGrid::kMinZ; z < CellGrid::kEndZ && next < kSlots; ++z) {
        for (std::int32_t x = CellGrid::kMinX; x < CellGrid::kEndX; ++x) {
            const std::int32_t file = map_cells().file_at({x, z});
            if (file == kNoFile || present({x, z})) {
                continue;
            }
            while (next < kSlots && slot_loaded(host, slots_[next])) {
                ++next;
            }
            if (next >= kSlots) {
                break;
            }
            Slot& s = slots_[next];
            s.material_count = 0;
            start_read(host, s, file, {x, z}, x0, z0, x1, z1);
            drain(host);
            if (slot_loaded(host, s)) {
                prepare_slot(host, next);
            }
            ++next;
        }
    }
    window_cell_ = cell_of(x1, z1);
    sample(host, x0, z0, x1, z1);
    apply_materials(host);
}

void CellStream::stream_window(CellStreamHost& host, std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1) {
    const CellCoord centre = cell_of(x0, z0);
    window_cell_ = cell_of(x1, z1);
    sample(host, x0, z0, x1, z1);
    apply_materials(host);

    // Interiors load the +-2 x / +-1 z window the draw uses; whole-town flight widens it with its own budget.
    const bool wide = host.free_camera() && whole_map_active(host);
    const std::int32_t lo = wide ? -4 : (exterior_ ? -1 : -2);
    const std::int32_t hi = wide ? 5 : (exterior_ ? 1 : 2);
    const std::int32_t budget = wide ? kWideLoadsPerUpdate : kLoadsPerUpdate;
    std::int32_t loads = 0;
    for (std::int32_t z = lo; z <= hi; ++z) {
        for (std::int32_t x = lo; x <= hi; ++x) {
            if (!(wide || exterior_ || (z >= -1 && z <= 1))) {
                continue;
            }
            const CellCoord cell{centre.x + x, centre.z + z};
            const std::int32_t file = map_cells().file_at(cell);
            if (file == kNoFile) {
                continue;
            }
            // Exteriors load the cells the first sample point reaches; for interiors the window itself is the gate.
            if (!(wide || !exterior_ || padded_distance_to_cell(x0, z0, cell, exterior_) <= 0) || present(cell)) {
                continue;
            }
            if (loads >= budget) {
                continue;
            }
            std::vector<WindowSlot> window(static_cast<std::size_t>(slot_count_));
            for (int i = 0; i < slot_count_; ++i) {
                window[i].occupied = slots_[i].ticket != kNoTicket;
                window[i].outside_count = slots_[i].outside_count;
                window[i].material_count = slots_[i].material_count;
                window[i].padded_distance = slots_[i].padded0;
            }
            const std::optional<std::size_t> pick = pick_window_slot(window, exterior_);
            if (!pick) {
                continue;
            }
            Slot& s = slots_[*pick];
            if (slot_loaded(host, s) && s.ready) {
                release(s.materials);
            }
            // Not the next eviction candidate in this same pass.
            s.material_count = 0;
            if (start_read(host, s, file, cell, x0, z0, x1, z1) != kNoTicket) {
                ++loads;
                // In the window by definition: a later load of this pass must not take it (a door moves the whole
                // window at once).
                s.outside_count = 0;
                s.padded0 = 0;
                s.padded1 = 0;
            }
        }
    }
}

void CellStream::update(CellStreamHost& host, std::int32_t x0, std::int32_t z0, std::int32_t x1, std::int32_t z1) {
    position_x_ = x1;
    position_z_ = z1;
    if (library_.ticket == kNoTicket) {
        library_.ticket = host.pace_read(library_.file, true);
        library_.ready = false;
    }
    if (host.preload_exteriors() && exterior_) {
        preload_all(host, x0, z0, x1, z1);
    } else {
        stream_window(host, x0, z0, x1, z1);
    }
    prepare_library(host);
    for (int i = 0; i < slot_count_; ++i) {
        if (slot_loaded(host, slots_[i])) {
            prepare_slot(host, static_cast<std::size_t>(i));
        }
    }
}

// ---- CellStream: which cells hold pool textures -------------------------------------------------------------------

void CellStream::apply_materials(CellStreamHost& host) {
    // Resident interiors and whole-town mode: every ready cell keeps its textures (whole-town only the outdoor cells
    // and those in reach, the first claims staggered nearest first).
    if ((!exterior_ && host.resident_textures()) || whole_map_active(host)) {
        std::array<Slot*, kClaimsPerUpdate> claims{};
        std::size_t claim_count = 0;
        const bool whole = exterior_;
        for (int i = 0; i < slot_count_; ++i) {
            Slot& s = slots_[i];
            if (!slot_loaded(host, s) || !s.ready) {
                continue;
            }
            if (whole) {
                if (s.padded0 > 0 && !outdoor(host, s.cell)) {
                    continue;
                }
                if (slot_state(host, static_cast<std::size_t>(i)) < LoadState::Loaded) {
                    std::size_t at = claim_count;
                    while (at > 0 && claims[at - 1]->padded0 > s.padded0) {
                        if (at < kClaimsPerUpdate) {
                            claims[at] = claims[at - 1];
                        }
                        --at;
                    }
                    if (at < kClaimsPerUpdate) {
                        claims[at] = &s;
                        if (claim_count < kClaimsPerUpdate) {
                            ++claim_count;
                        }
                    }
                    continue;
                }
            }
            load_cell_materials(host, s);
        }
        for (std::size_t i = 0; i < claim_count; ++i) {
            load_cell_materials(host, *claims[i]);
        }
        return;
    }

    if (!exterior_) {
        // The nearest ready cells keep textures, nearest first; a nearer cell short of a texture takes the pages of
        // the farthest kept ones (steals only flow nearer <- farther).
        std::array<Slot*, kInteriorTextured> keep{};
        std::size_t kept = 0;
        for (int i = 0; i < slot_count_; ++i) {
            Slot& s = slots_[i];
            if (!slot_loaded(host, s) || !s.ready) {
                continue;
            }
            const std::int32_t d = std::min(s.padded0, s.padded1);
            std::size_t at = 0;
            for (; at < kept; ++at) {
                if (d < std::min(keep[at]->padded0, keep[at]->padded1)) {
                    break;
                }
            }
            if (at < kInteriorTextured) {
                for (std::size_t j = std::min(kept, kInteriorTextured - 1); j > at; --j) {
                    keep[j] = keep[j - 1];
                }
                keep[at] = &s;
                if (kept < kInteriorTextured) {
                    ++kept;
                }
            }
        }
        for (int i = 0; i < slot_count_; ++i) {
            Slot& s = slots_[i];
            if (!slot_loaded(host, s) || !s.ready) {
                continue;
            }
            if (std::find(keep.begin(), keep.begin() + static_cast<std::ptrdiff_t>(kept), &s) ==
                keep.begin() + static_cast<std::ptrdiff_t>(kept)) {
                release(s.materials);
            }
        }
        for (std::size_t at = 0; at < kept; ++at) {
            load_cell_materials(host, *keep[at]);
            for (std::size_t from = kept - 1; from > at; --from) {
                const bool missing = std::any_of(keep[at]->materials.begin(), keep[at]->materials.end(),
                                                 [](const Material& m) { return !m.preset && m.texture == nullptr; });
                if (!missing) {
                    break;
                }
                release(keep[from]->materials);
                load_cell_materials(host, *keep[at]);
            }
        }
        return;
    }

    // Exteriors: the cells the sample points reach keep textures, the others give them back; this reach is the draw
    // distance of the scenery (an untextured cell is not drawn). Distances are Q8.
    std::int32_t reach = host.preload_exteriors() && host.free_camera() ? 2 * kCellSizeGeometry : 0;
    if (host.draw_distance_pct() > 100 && reach == 0) {
        const std::int32_t far = ((0x79C << 3) * host.draw_distance_pct()) / 100 - kCellSizeGeometry;
        reach = far > 0 ? far : 0;
    }
    for (int i = 0; i < slot_count_; ++i) {
        Slot& s = slots_[i];
        if (slot_loaded(host, s) && s.ready && s.padded0 > reach && s.padded1 > reach) {
            release(s.materials);
        }
    }
    for (int i = 0; i < slot_count_; ++i) {
        Slot& s = slots_[i];
        if (slot_loaded(host, s) && s.ready && (s.padded0 <= reach || s.padded1 <= reach)) {
            load_cell_materials(host, s);
        }
    }
}

// ---- CellStream: states ----------------------------------------------------------------------------------------------

LoadState CellStream::slot_state(CellStreamHost& host, std::size_t index) {
    const Slot& s = slots_[index];
    const LoadState read = ticket_state(host, s.ticket);
    if (read != LoadState::Loaded) {
        return read;
    }
    return s.ready && textures_loaded(host, s.materials) ? LoadState::Loaded : LoadState::Corrupted;
}

LoadState CellStream::library_state(CellStreamHost& host) {
    const LoadState read = ticket_state(host, library_.ticket);
    if (read != LoadState::Loaded) {
        return read;
    }
    return library_.ready && textures_loaded(host, library_.materials) ? LoadState::Loaded : LoadState::Corrupted;
}

bool CellStream::library_ready(CellStreamHost& host) {
    const LoadState read = ticket_state(host, library_.ticket);
    if (read == LoadState::Unloaded) {
        return false;
    }
    return read == LoadState::Invalid || library_.ready;
}

bool CellStream::window_loaded(CellStreamHost& host) {
    const LoadState lib = library_state(host);
    if (lib == LoadState::Unloaded || lib == LoadState::Corrupted) {
        return false;
    }
    for (int i = 0; i < slot_count_; ++i) {
        const LoadState st = slot_state(host, static_cast<std::size_t>(i));
        if (st == LoadState::Invalid || st == LoadState::Loaded) {
            continue;
        }
        if (slots_[i].padded0 <= 0 || slots_[i].padded1 <= 0) {
            return false;
        }
    }
    return true;
}

// ---- CellStream: queries ---------------------------------------------------------------------------------------------

int CellStream::object_library(CellStreamHost& host, std::string_view name, std::int32_t x, std::int32_t z) {
    if (ticket_state(host, library_.ticket) == LoadState::Loaded && library_.ready && host.library_has_object(2, name)) {
        return 2;
    }
    const std::int32_t gx = x >> 4;
    const std::int32_t gz = z >> 4;
    const CellCoord cell = cell_of(x, z);
    // Up to the 3 x 3 cells around the point, nearest first (interiors: the point's cell only).
    std::array<std::size_t, 9> found{};
    std::array<std::int32_t, 9> dist{};
    std::size_t count = 0;
    for (int i = 0; i < slot_count_; ++i) {
        const Slot& s = slots_[i];
        if (!slot_loaded(host, s) || !s.ready) {
            continue;
        }
        if (!exterior_) {
            if (s.cell == cell) {
                found[count++] = static_cast<std::size_t>(i);
                break;
            }
            continue;
        }
        if (s.cell.x < cell.x - 1 || s.cell.x > cell.x + 1 || s.cell.z < cell.z - 1 || s.cell.z > cell.z + 1 ||
            count >= found.size()) {
            continue;
        }
        const std::int32_t d = distance_to_cell(gx, gz, s.cell);
        std::size_t at = 0;
        while (at < count && !(d < dist[at])) {
            ++at;
        }
        for (std::size_t j = count; j > at; --j) {
            dist[j] = dist[j - 1];
            found[j] = found[j - 1];
        }
        dist[at] = d;
        found[at] = static_cast<std::size_t>(i);
        ++count;
    }
    for (std::size_t k = 0; k < count; ++k) {
        const int library = static_cast<int>(found[k]) + 3;
        if (host.library_has_object(library, name)) {
            return library;
        }
    }
    return 0;
}

bool CellStream::near_window(std::int32_t x, std::int32_t z) const {
    const CellCoord cell = cell_of(x, z);
    if (exterior_) {
        // 4.5 m in Q8.
        return distance_to_cell(position_x_ >> 4, position_z_ >> 4, cell) <= 1152;
    }
    return cell == window_cell_;
}

bool CellStream::outdoor(CellStreamHost& host, CellCoord cell) {
    if (!CellGrid::contains(cell)) {
        return false;
    }
    if (!outdoor_valid_) {
        outdoor_valid_ = true;
        RoomAt room_at;
        if (host.has_rooms()) {
            room_at = [&host](std::int32_t x, std::int32_t z) { return host.room_at(x, z); };
        }
        outdoor_.classify(map_cells(), parked_, room_at);
        if (room_at && host.whole_map_enabled() && exterior_) {
            host.log("[WHOLEMAP] room table: " + std::to_string(outdoor_.count()) + " outdoor cells");
        }
    }
    return outdoor_.outdoor(cell);
}

bool CellStream::whole_map_active(CellStreamHost& host) {
    if (!(host.whole_map_enabled() && host.preload_exteriors() && host.resident_textures() && exterior_)) {
        return false;
    }
    // The player's cell, not the window's (the window's sample point runs ahead of the player near edges).
    return outdoor(host, host.player_cell());
}

bool CellStream::cell_drawn(CellStreamHost& host, std::size_t index) {
    const Slot& s = slots_[index];
    if (host.free_camera() && whole_map_active(host)) {
        return true;
    }
    if (exterior_) {
        return true;
    }
    // Interiors: the player's own cell, and the cells of the same room around it within 2 cells (a wide view sees
    // past the cell edge) -- but not in the boss arenas, where the same-room test cannot tell the neighbours apart.
    if (s.cell == window_cell_) {
        return true;
    }
    if (host.exact_cell_arena()) {
        return false;
    }
    const std::int32_t dx = s.cell.x - window_cell_.x;
    const std::int32_t dz = s.cell.z - window_cell_.z;
    if (dx >= -2 && dx <= 2 && dz >= -2 && dz <= 2 && host.has_rooms()) {
        const std::int32_t half = kCellSize / 2;
        const std::uint8_t player_room = host.room_at(window_cell_.x * kCellSize + half, window_cell_.z * kCellSize + half);
        const std::uint8_t cell_room = host.room_at(s.cell.x * kCellSize + half, s.cell.z * kCellSize + half);
        return cell_room == player_room;
    }
    return false;
}

std::vector<ResidentCell>& CellStream::resident_cells(CellStreamHost& host) {
    resident_.resize(static_cast<std::size_t>(slot_count_));
    for (int i = 0; i < slot_count_; ++i) {
        resident_[i].cell = slots_[i].cell;
        resident_[i].ready = slot_loaded(host, slots_[i]) && slots_[i].ready;
    }
    return resident_;
}

std::string CellStream::describe(CellStreamHost& host) {
    std::string out = "lib=" + std::to_string(library_.file) + ":" + std::to_string(static_cast<int>(library_state(host)));
    for (int i = 0; i < slot_count_; ++i) {
        const Slot& s = slots_[i];
        if (s.ticket == kNoTicket) {
            continue;
        }
        out += " " + std::to_string(i) + "@" + std::to_string(s.cell.x) + "," + std::to_string(s.cell.z) + ":" +
               std::to_string(static_cast<int>(slot_state(host, static_cast<std::size_t>(i)))) + "/" +
               std::to_string(s.material_count) + "/" + std::to_string(s.outside_count);
    }
    out += " pool";
    for (TexturePool::List list : {TexturePool::List::Full, TexturePool::List::Half}) {
        for (std::size_t i = 0; i < pool_.listed(list); ++i) {
            const PoolTexture* t = pool_.entry(list, i);
            if (t->ref_count != 0 || t->ticket != kNoTicket) {
                out += " " + std::string(texture_name_view(t->name)) + ":" + std::to_string(t->ref_count);
            }
        }
    }
    return out;
}

CellStream& cell_stream() {
    static CellStream stream;
    return stream;
}

}  // namespace sh::world
