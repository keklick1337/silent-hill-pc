// The game's character spawn calls on the engine (src/engine/gameflow/npc_spawn.h section
// 23): the slot records are in the session's character block; the spawn-group mask and the room cap are the
// session's. The loader around a spawn (model load, bone init) is another module's.

#include <cstdint>

#include "engine/debug/dbg_trace.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/npc_spawn.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_runtime.h"

namespace {

namespace gf = sh::gameflow;
namespace link = sh::gameflow::game;

// The group characters end at Cybil's monster form; the room spawner owns the first 32 slots.
constexpr int kLastGroupChara = 24;
constexpr int kLegacySlots = 32;
const gf::NpcSpawnRules kRules{kLastGroupChara, kLegacySlots};

std::uint32_t& spawned_groups() {
    return reinterpret_cast<std::uint32_t&>(sh::game::session().system.scene.spawned_groups);
}

class GameNpcSlots final : public gf::NpcSpawnHost {
public:
    int slot_chara(int slot) const override { return link::npc_chara(slot); }
    int slot_group(int slot) const override { return link::npc_group(slot); }
    // The mask is one word: groups past 31 read and write the word after it, as the original's bit array.
    bool group_spawned(int group) const override {
        return ((&spawned_groups())[group >> 5] >> (group & 0x1F) & 1u) != 0;
    }
    void set_group_spawned(int group, bool on) override {
        if (on) {
            (&spawned_groups())[group >> 5] |= 1u << (group & 0x1F);
        } else {
            (&spawned_groups())[group >> 5] &= ~(1u << (group & 0x1F));
        }
    }
    int room_cap() const override { return sh::game::session().system.scene.npc_flags_owner; }
    std::int32_t ground_height(std::int32_t x, std::int32_t z) override { return link::collision_ground(x, z); }
    void fill_slot(int slot, const gf::NpcSlotFill& fill) override { link::npc_fill(slot, fill); }
};

int spawn_row_group(int chara_id) { return link::chara_anim_data_index(chara_id) - 1; }

}  // namespace

extern "C" {

unsigned int Chara_Load(std::int32_t modelIdx, std::int32_t charaId, void* boneCoords, std::int8_t forceFree,
                        void* lmHdr, void* tex) {
    link::chara_load(modelIdx, charaId, boneCoords, forceFree, lmHdr, tex);
    return true;
}

unsigned int Chara_ProcessLoads(void) {
    link::chara_process_loads();
    return true;
}

void Chara_BonesInit(std::int32_t idx) {
    link::chara_bones_init(idx + 1);
}

std::int32_t Chara_Spawn(std::int32_t charaId, std::int32_t spawnFlags, std::int32_t posX, std::int32_t posZ,
                         std::int16_t rotY, std::uint32_t stateStep) {
    GameNpcSlots slots;
    gf::NpcSpawnRequest request;
    request.chara_id = charaId;
    request.flags = spawnFlags;
    request.x = posX;
    request.z = posZ;
    request.yaw = rotY;
    request.state_step = stateStep;
    const int slot = gf::spawn_npc(slots, kRules, request);
    if (slot < kLegacySlots && link::npc_chara(slot) == charaId) {
        SH_LOG_DEBUG(SPAWN, "Chara_Spawn %s (id %d) slot %d flags 0x%X at (%.2f, %.2f) step %u",
                     DbgTrace_CharaName(charaId), static_cast<int>(charaId), slot, static_cast<unsigned>(spawnFlags),
                     posX / 4096.0f, posZ / 4096.0f, static_cast<unsigned>(stateStep));
    }
    return slot;
}

void Chara_ModelCharaIdClear(void* chara, std::int32_t unused0, std::int32_t unused1) {
    (void)unused0;
    (void)unused1;
    if (chara == nullptr) {
        return;
    }
    GameNpcSlots slots;
    gf::release_npc_group(slots, kRules, link::chara_record_id(chara), link::chara_record_group(chara));
    link::set_chara_record_id(chara, 0);
}

// The map's spawn table has two group rows; characters of other animation slots have none.
void Chara_SpawnFlagsSet(std::int32_t charaId, std::int32_t spawnIdx, std::int32_t spawnFlags) {
    if (spawn_row_group(charaId) > 1) {
        SH_DBG("[POOL] Chara_SpawnFlagsSet: chara %d animIdx %d has no spawn-table row - skipped", charaId,
               link::chara_anim_data_index(charaId));
        return;
    }
    sh::maps::map_runtime().spawn(spawn_row_group(charaId), spawnIdx).flags = static_cast<std::int8_t>(spawnFlags);
}

void Chara_SpawnPositionSet(std::int32_t charaId, std::int32_t spawnIdx, std::int32_t posX, std::int32_t posZ) {
    if (spawn_row_group(charaId) > 1) {
        SH_DBG("[POOL] Chara_SpawnPositionSet: chara %d animIdx %d has no spawn-table row - skipped", charaId,
               link::chara_anim_data_index(charaId));
        return;
    }
    sh::maps::SpawnPoint& info = sh::maps::map_runtime().spawn(spawn_row_group(charaId), spawnIdx);
    info.x = posX;
    info.z = posZ;
}

}  // extern "C"
