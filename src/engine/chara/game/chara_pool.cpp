// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The character pool: the assets of a type the current map did not load, for the engine's spawns (the
// console's `spawn`, the debug menu, a script's sh.spawn) and for the types mods add.
//
// A type is visible only when two registries hold it: its model and its animation slot. The original loads about
// three enemy types per map into fixed memory windows; a foreign type does not fit there, so the pool keeps it on
// the PC side, loaded on the first spawn that asks for it:
//   - model and animation files in buffers of their real size;
//   - animations in slots past the map's groups (the slot of type N is the groups' count + N), loaded through the
//     game's explicit-buffer path; the map's own code only walks its group slots, so the pool's are invisible to it;
//   - textures in virtual texture slots (256 + type), named by a synthetic palette row past the console's video
//     memory, so the draw path needs no change.
// A map keeps its own slots and variants: only the registries the map left empty or stale are filled, so a game
// that never spawns a foreign type is untouched. What a spawned character runs is chosen per NPC slot
// (g_PcNpcSpawnAi, chara_spawn.h).

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/app/loader/game_files.h"
#include "engine/chara/chara_library.h"
#include "engine/chara/game/chara_entry.h"
#include "engine/chara/game/chara_link.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/entities/character_record.h"
#include "engine/entities/game/entity_host.h"
#include "engine/game_state/game_ids.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/log/sh_log.h"
#include "engine/platform/disc.h"
#include "render/geometry/oversized_models.h"

extern "C" {
unsigned char g_PcNpcDebugSpawned[sh::entities::kNpcSlotCount];
unsigned char g_PcNpcSpawnAi[sh::entities::kNpcSlotCount];
}

namespace {

namespace cg = sh::chara::game;
namespace ids = sh::game_state::chara;
namespace link = sh::chara::game::link;

// The types the pool loads: character.json "pool" (by default the types with a "spawn" block: the enemies and the
// cutscene cast); a mod's new type too (its number is past the original's).
bool pooled(int id) {
    if (id < 0 || id >= ids::kMaxTypes) {
        return false;
    }
    const sh::chara::CharacterDef* def = sh::chara::type_def(id);
    return def != nullptr && def->pooled();
}
// One posed skeleton per type: the model's bone cap and the root.
constexpr int kPoolBones = 57;
// The synthetic texture page of a pool texture (only the page bits of the draw read it; the override ignores them).
constexpr std::uint8_t kPoolTexturePage = 28;
// The queue pump's safety limit for one synchronous load.
constexpr int kPumpLimit = 100000;
// Readable bytes kept behind an oversized model file in its buffer.
constexpr std::int32_t kModelTail = static_cast<std::int32_t>(sh::models::OversizedModels::kTail);
// A pool type's disc texture is read to a virtual slot: a place whose palette row no video memory has, so the read
// uploads nothing (the scene draws the type from its glb and the imported pictures). The slot number is spread over
// the palette x cell (64 values) and groups of 16 rows from row 512 on; 512 slots exist. The base types take the
// slots from 256 on, one per type number.
constexpr int kVirtualSlots = 512;
constexpr int kVirtualPaletteRow = 512;
constexpr int kVirtualRowGroup = 16;
constexpr int kBaseTextureSlotBase = 256;
// Virtual texture slots of the mods' new types: past the base types' slots and the rows a base type with many
// palettes reaches, one each while they last; a type past them reads no disc texture.
constexpr int kModTextureSlotBase = 448;
constexpr int kModTextureSlots = kVirtualSlots - kModTextureSlotBase;

// The virtual texture slot of a pool type, -1 for none.
int texture_slot(int id) {
    if (id < ids::kCount) {
        return kBaseTextureSlotBase + id;
    }
    const int k = id - ids::kCount;
    return k < kModTextureSlots ? kModTextureSlotBase + k : -1;
}

// A mod's new type poses from its own animation file (character.json "animations"), not from the carrier disc
// file its manifest names: the game path of that file ("ANIM/GHOST.ANM" for anim/ANIM/GHOST.anim.json), or empty.
std::string mod_anim_game_path(int id) {
    if (id < ids::kCount) {
        return {};
    }
    const sh::chara::CharacterDef* def = sh::chara::type_def(id);
    constexpr std::string_view kAnim = "anim/";
    constexpr std::string_view kJson = ".anim.json";
    if (def == nullptr || !def->animations.starts_with(kAnim) || !def->animations.ends_with(kJson)) {
        return {};
    }
    return def->animations.substr(kAnim.size(), def->animations.size() - kAnim.size() - kJson.size()) + ".ANM";
}

// Pool-owned storage of the model record (its size comes from the link).
class ModelRecord {
public:
    void* get() {
        if (!bytes_) {
            const std::size_t n = (link::chara_model_bytes() + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
            bytes_ = std::make_unique<std::max_align_t[]>(n);
            std::memset(bytes_.get(), 0, n * sizeof(std::max_align_t));
        }
        return bytes_.get();
    }

private:
    std::unique_ptr<std::max_align_t[]> bytes_;
};

struct PoolEntry {
    ModelRecord model;
    std::vector<std::uint8_t> lm;  // the model file
    std::vector<std::uint8_t> anim;
    // One posed skeleton (allocated on the first load; the animation slot keeps pointing at it).
    std::unique_ptr<sh::entities::BoneCoord[]> bones;
    link::CharaFiles files;  // what it was loaded from: a later map that retargets the type reloads it
    bool loaded = false;
};

class CharaPool {
public:
    // The files the pool loads for a type: the file table's row, with the beta retargets. Pool only: a map loads
    // the retail files (the table itself is retargeted only inside a load's synchronous window).
    static link::CharaFiles files_for(int id) {
        link::CharaFiles f = link::chara_files(id);
        // character.json "pool_files": the beta Puppet Nurse (TEST/PRS2.ILM, on every retail disc) rides on the
        // DummyNurse, a retail placeholder without texture that already runs the Puppet Nurse's behaviour.
        if (const sh::chara::CharacterDef* def = sh::chara::type_def(id)) {
            const sh::loader::DiscFiles& disc = sh::loader::disc_files();
            if (!def->pool_model.empty()) {
                f.model = static_cast<std::int16_t>(disc.find(def->pool_model));
            }
            if (!def->pool_texture.empty()) {
                f.texture = static_cast<std::int16_t>(disc.find(def->pool_texture));
            }
            if (!def->pool_anim.empty()) {
                f.anim = static_cast<std::int16_t>(disc.find(def->pool_anim));
            }
        }
        return f;
    }

    bool load(int id) {
        if (id < 0 || id >= ids::kMaxTypes) {
            return false;
        }
        PoolEntry& p = entries_[static_cast<std::size_t>(id)];
        const link::CharaFiles want = files_for(id);
        if (want.model == -1 || want.anim == -1) {
            return false;
        }
        const sh::loader::DiscFiles& disc = sh::loader::disc_files();
        // A validated oversized loose model sizes the buffer from the real file (with the tail slack of the
        // three-at-a-time vertex strides); <= 0: absent or rejected by the validator.
        const std::int32_t loose = sh::models::oversized_models().accepted_bytes(want.model);
        auto lm_size = static_cast<std::int32_t>(disc.sector_bytes(want.model));
        if (loose > 0 && loose + kModelTail > lm_size) {
            lm_size = loose + kModelTail;
        }
        const auto anim_size = static_cast<std::int32_t>(disc.sector_bytes(want.anim));
        if (lm_size <= 0 || anim_size <= 0) {
            return false;  // zero-length rows exist in the file table
        }
        if (p.lm.size() < static_cast<std::size_t>(lm_size)) {
            // Drop the registration before the buffer moves: a stale pointer would lift the size gate for whatever
            // allocation later reuses that address.
            sh::models::oversized_models().set_game_buffer(id, nullptr, 0);
            p.lm.assign(static_cast<std::size_t>(lm_size), 0);
        }
        if (p.anim.size() < static_cast<std::size_t>(anim_size)) {
            p.anim.assign(static_cast<std::size_t>(anim_size), 0);
        }
        // Lifts the loose replace size gate for this destination by the current file's validated extent (never the
        // buffer's capacity, so a rejected file cannot inherit an earlier file's).
        if (loose > 0) {
            sh::models::oversized_models().set_game_buffer(id, p.lm.data(), static_cast<std::size_t>(loose) + kModelTail);
        } else {
            sh::models::oversized_models().set_game_buffer(id, nullptr, 0);
        }

        // The virtual slot place: the palette word carries the slot number.
        const int slot = texture_slot(id);
        sh::loader::ImagePlace place{};
        place.page[0] = 0;
        place.page[1] = kPoolTexturePage;
        if (slot >= 0) {
            place.palette_x = static_cast<std::int16_t>((slot % 64) * 16);
            place.palette_y =
                static_cast<std::int16_t>(kVirtualPaletteRow + (slot / 64) * kVirtualRowGroup);
        }

        // A reload means the files changed (region or ending patch): force a fresh read of the animations (the
        // explicit-buffer path would see the same buffer and skip the read).
        const int pool_slot = link::pool_anim_index(id);
        if (p.loaded) {
            if (p.files.anim != want.anim) {
                link::anim_slot_clear(pool_slot);
            }
            p.loaded = false;
        }

        void* model = p.model.get();
        link::chara_model_reset(model, id, p.lm.data(), &place);
        link::chara_model_set_ticket(model, sh::loader::queue_read(want.model, p.lm.data()));
        if (want.texture != -1 && slot >= 0) {
            link::chara_model_set_ticket(model, sh::loader::queue_read_image(want.texture, link::image_read_buffer(), &place));
        }

        // The animation path redirects the type's slot to the pool's: a live map slot is put back right after, so
        // a map keeps its own binding (spawn rows, bone chain) with the pool on.
        const int previous = link::anim_index(id);
        // The file table row is retargeted for the synchronous load only: the animation and material paths read it
        // by type. Put back right after, so a map's own loads stay retail.
        const link::CharaFiles saved = link::chara_files(id);
        link::set_chara_files(id, want);
        if (!p.bones) {
            p.bones = std::make_unique<sh::entities::BoneCoord[]>(kPoolBones);
        }
        link::anim_alloc(pool_slot, id, p.anim.data(), p.bones.get());
        // Synchronous: the queue is pumped directly (waiting a vertical blank per state stretched a map load to a
        // minute over ~42 types).
        for (int pump = 0; sh::loader::queue_length() > 0 && pump < kPumpLimit; ++pump) {
            sh::loader::queue_update();
        }
        link::chara_model_process_load(model);
        link::set_chara_files(id, saved);
        if (live_map_slot(id, previous)) {
            link::set_anim_index(id, previous);
        }

        if (!link::chara_model_loaded(model) || link::anim_slot(pool_slot).header == nullptr) {
            SH_DBG("[POOL] chara %d load FAILED (model=%d anm=%p)", id, link::chara_model_loaded(model) ? 1 : 0,
                   link::anim_slot(pool_slot).header);
            // Undo a half registration so the spawn gates report not ready instead of offering an invisible spawn;
            // the next map load retries.
            if (link::anim_index(id) == pool_slot) {
                link::set_anim_index(id, -1);
            }
            if (link::registered_model(id) == model) {
                link::set_registered_model(id, nullptr);
            }
            return false;
        }
        // A mod's type: the engine samples its own animation file over the carrier's bytes (same skeleton).
        if (const std::string own = mod_anim_game_path(id); !own.empty()) {
            sh::entities::game::anim_file_rebind(p.anim.data(), p.anim.size(), own);
        }
        p.files = want;
        p.loaded = true;
        return true;
    }

    // Registers a loaded entry where the map left the registries empty or stale.
    void register_type(int id) {
        PoolEntry& p = entries_[static_cast<std::size_t>(id)];
        if (!p.loaded) {
            return;
        }
        // Model: a live map registration wins. A map change leaves the previous map's registrations pointing at
        // records that now hold another type, so non-null is not enough: the record must still hold this type.
        void* m = link::registered_model(id);
        if (m == nullptr || link::model_chara(m) != id) {
            link::set_registered_model(id, p.model.get());
        }
        // Animation: a live map binding wins; anything stale (never set, or a group slot another type now owns)
        // goes to the pool slot. A map with fewer groups keeps the previous map's data in its unused slots, which may
        // be reused mid-map, so they are never scanned for.
        const int pool_slot = link::pool_anim_index(id);
        const link::AnimSlot pool = link::anim_slot(pool_slot);
        if (!live_map_slot(id, link::anim_index(id)) && pool.chara == id && pool.header != nullptr) {
            link::set_anim_index(id, pool_slot);
        }
    }

    bool loaded(int id) const { return entries_[static_cast<std::size_t>(id)].loaded; }
    const link::CharaFiles& files(int id) const { return entries_[static_cast<std::size_t>(id)].files; }
    bool is_pool_model(int id) { return link::registered_model(id) == entries_[static_cast<std::size_t>(id)].model.get(); }

private:
    static bool live_map_slot(int id, int index) {
        if (index < 1 || index >= link::anim_group_count()) {
            return false;
        }
        const link::AnimSlot s = link::anim_slot(index);
        return s.chara == id && s.header != nullptr;
    }

    PoolEntry entries_[ids::kMaxTypes];
};

CharaPool& pool() {
    static CharaPool* p = new CharaPool();
    return *p;
}

// Synchronous loads at disk speed: the original never loads this pool, so it has no drive timing to keep.
class InstantDrive {
public:
    InstantDrive() : previous_(sh::disc::drive_pacing().instant) { sh::disc::drive_pacing().instant = true; }
    ~InstantDrive() { sh::disc::drive_pacing().instant = previous_; }
    InstantDrive(const InstantDrive&) = delete;
    InstantDrive& operator=(const InstantDrive&) = delete;

private:
    bool previous_;
};

}  // namespace

extern "C" {

void Pc_NpcDebugSpawnClearAll(void) {
    std::memset(g_PcNpcDebugSpawned, 0, sizeof(g_PcNpcDebugSpawned));
    std::memset(g_PcNpcSpawnAi, 0, sizeof(g_PcNpcSpawnAi));
}

int Pc_CharaPool_IsPoolModel(int charaId) {
    if (charaId < 0 || charaId >= ids::kMaxTypes) {
        return 0;
    }
    return pool().is_pool_model(charaId) ? 1 : 0;
}

int Pc_CharaPool_Ensure(int id) {
    if (!pooled(id)) {
        return 0;
    }
    // A map whose file table retargets the type (region or ending variants) gets a fresh load.
    const link::CharaFiles want = CharaPool::files_for(id);
    const link::CharaFiles& have = pool().files(id);
    if (!pool().loaded(id) || have.model != want.model || have.texture != want.texture || have.anim != want.anim) {
        const InstantDrive instant;
        if (!pool().load(id)) {
            return 0;
        }
    }
    pool().register_type(id);
    return 1;
}

}  // extern "C"
