// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The character animation slots of the running game (engine/gameflow/anim_slots.h): the
// slot table and the character -> slot index are stored here, the host gives the slot logic the session's bone
// pool, the file queue and the bind pose.

#include <cstdint>
#include <cstring>
#include <optional>

#include "engine/app/loader/game_files.h"
#include "engine/entities/bind_pose.h"
#include "engine/entities/game/model_runtime.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/anim_slots.h"
#include "engine/log/sh_log.h"
#include "engine/memory/work_buffers.h"
#include "engine/maps/map_runtime.h"

namespace ent = sh::entities;
namespace gf = sh::gameflow;

extern "C" {

// A file copy between animation buffers: the animation bank follows its files.
void PcEntityAnim_MemoryCopied(void* dest, const void* src, unsigned int size);
// The looping clip playback (engine/entities/game/model_runtime.cpp).
void Anim_PlaybackLoop(ent::ModelRecord* model, void* anm_header, ent::BoneCoord* bones, ent::ClipInfoRecord* clip);

}  // extern "C"

namespace {

constexpr int kGroupSlots = gf::AnimSlotTable::kGroupSlots;
// The slots made up front: the groups' and one per type number the base game and a few mods use; more on demand.
constexpr int kInitialCharaTypes = 256;
constexpr int kHarry = 1;
constexpr std::int32_t kHarryAnimSize = 0x2E630;  // the reserved size of Harry's base animations

// Where the game keeps Harry's base animation file.
void* player_anim_buffer() { return sh::memory::work_buffers().data(sh::memory::WorkBuffer::PlayerBaseAnim); }

class GameAnimHost final : public gf::AnimSlotHost {
public:
    std::int32_t anim_file_size(int chara) const override {
        return sh::game_state::file_sector_size(sh::game_state::chara_anim_file(chara));
    }
    int bone_count(const void* header) const override {
        return static_cast<const std::uint8_t*>(header)[6];  // the ANM header's bone count
    }
    void* coord_advance(void* coord, int count) const override { return static_cast<ent::BoneCoord*>(coord) + count; }
    bool coord_past_end(const void* coord, int bones) const override {
        const ent::BoneCoord* pool = sh::game::session().system.characters.npc_bone_pool;
        return (&static_cast<const ent::BoneCoord*>(coord)[bones] + 1) >= &pool[ent::kNpcBonePoolSize];
    }
    void* coord_buffer_start() const override { return &sh::game::session().system.characters.npc_bone_pool[0]; }
    // The game's spill buffer is the pool itself: a full pool starts over at its first bone.
    void* coord_buffer_spill() const override { return &sh::game::session().system.characters.npc_bone_pool[0]; }
    void bind_bones(void* header, void* coord) override {
        ent::bind_pose_init(header, static_cast<ent::BoneCoord*>(coord));
    }
    void move_bytes(void* dest, void* src, std::int32_t bytes) override {
        if (const std::optional<std::size_t> room = sh::memory::work_buffers().room(dest);
            room && bytes > 0 && static_cast<std::size_t>(bytes) > *room) {
            SH_ERR("[ANIM] a move of %d animation bytes does not fit the animation area (%zu bytes); clipped", bytes, *room);
            bytes = static_cast<std::int32_t>(*room);
        }
        // Whole 32-bit words only, as the original word copier moved them.
        if (bytes > 0) {
            std::memmove(dest, src, static_cast<std::size_t>(bytes) & ~static_cast<std::size_t>(3));
        }
        PcEntityAnim_MemoryCopied(dest, src, static_cast<unsigned int>(bytes));
    }
    void queue_read(int slot, int chara, void* dest, void* coord) override {
        sh::loader::queue_read_anim(slot, chara, dest, coord);
    }
    void set_chara_slot(int chara, int slot) override;
    int group_chara(int group) const override { return sh::maps::map_runtime().group_chara(group); }
};

GameAnimHost s_host;

}  // namespace

namespace {

// Every type starts without a slot (-1).
gf::AnimSlotTable s_table = [] {
    gf::AnimSlotTable t{};
    t.slots.assign(kGroupSlots + kInitialCharaTypes, gf::AnimSlot{});
    t.chara_slot.assign(kInitialCharaTypes, -1);
    t.slots[0] = {kHarry, kHarry, nullptr, nullptr, kHarryAnimSize, kHarryAnimSize, nullptr};
    return t;
}();

void GameAnimHost::set_chara_slot(int chara, int slot) { s_table.set_chara_slot(chara, slot); }

gf::AnimSlots s_slots(s_table.slots, kGroupSlots, s_host);

}  // namespace

gf::AnimSlotTable& gf::anim_slot_table() { return s_table; }
gf::AnimSlots& gf::anim_slots() { return s_slots; }

extern "C" {

// The player's slot points at his animation buffer (the buffer's address is known only at run time).
void PcPort_InitCharaAnimInfo(void) {
    s_table.slots[0].alloc_addr = player_anim_buffer();
    s_table.slots[0].active_header = player_anim_buffer();
}

// The map's three character group slots (1..3) empty.
void Anim_CharaTypeAnimInfoClear(void) {
    std::memset(&s_table.slots[1], 0, sizeof(gf::AnimSlot) * (kGroupSlots - 1));
}

}  // extern "C"
