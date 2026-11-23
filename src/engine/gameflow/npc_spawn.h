#pragma once

// Putting a character into an NPC slot of the room spawner: map scripts and
// cutscenes spawn their cast this way. Characters up to `last_group_chara` belong to a spawn group (0..31, from the
// low bits of the spawn flags): a group spawns once per session (the session's group mask), and the room's NPC cap
// limits how many are live. Other characters only need a free slot. The host owns the slot records.

#include <cstdint>

namespace sh::gameflow {

struct NpcSpawnRules {
    int last_group_chara = 0;  // characters 0..this belong to spawn groups (the game's monsters and Cybil)
    int room_slots = 32;       // slots the room spawner and the group mask cover
};

struct NpcSpawnRequest {
    int chara_id = 0;
    int flags = 0;  // spawn flags; the low 5 bits are the group (only below 64)
    std::int32_t x = 0, z = 0;
    std::int16_t yaw = 0;
    std::uint32_t state_step = 0;
};

// What a fresh slot gets; everything else in it is cleared.
struct NpcSlotFill {
    int chara_id = 0;
    int spawn_group = 0;
    std::uint32_t state_step = 0;
    std::int32_t x = 0, y = 0, z = 0;
    std::int16_t yaw = 0;
};

class NpcSpawnHost {
public:
    virtual ~NpcSpawnHost() = default;
    virtual int slot_chara(int slot) const = 0;  // 0 = empty
    virtual int slot_group(int slot) const = 0;
    virtual bool group_spawned(int group) const = 0;
    virtual void set_group_spawned(int group, bool on) = 0;
    virtual int room_cap() const = 0;  // live group characters allowed in the room
    virtual std::int32_t ground_height(std::int32_t x, std::int32_t z) = 0;
    // Clears the slot, writes `fill`, marks it live and visible.
    virtual void fill_slot(int slot, const NpcSlotFill& fill) = 0;
};

// Returns the slot spawned into; for a group already spawned, the slot holding that group (or room_slots when none
// does); 0 when the room cap is reached (as the game does); room_slots when no slot is free.
int spawn_npc(NpcSpawnHost& host, const NpcSpawnRules& rules, const NpcSpawnRequest& request);

// A slot's character is being removed: its group may spawn again.
void release_npc_group(NpcSpawnHost& host, const NpcSpawnRules& rules, int chara_id, int spawn_group);

}  // namespace sh::gameflow
