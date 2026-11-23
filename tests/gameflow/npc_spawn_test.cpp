// NPC slot spawning against its specification (the original's Chara_Spawn): groups spawn once per session, the room cap,
// the first free slot, the ground height, releasing a group.

#include "engine/gameflow/npc_spawn.h"

#include <array>

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

struct Slots : gf::NpcSpawnHost {
    std::array<gf::NpcSlotFill, 32> slots{};
    std::uint32_t mask = 0;
    int cap = 32;
    int slot_chara(int s) const override { return slots[static_cast<std::size_t>(s)].chara_id; }
    int slot_group(int s) const override { return slots[static_cast<std::size_t>(s)].spawn_group; }
    bool group_spawned(int g) const override { return ((mask >> g) & 1u) != 0; }
    void set_group_spawned(int g, bool on) override { mask = on ? mask | (1u << g) : mask & ~(1u << g); }
    int room_cap() const override { return cap; }
    std::int32_t ground_height(std::int32_t x, std::int32_t z) override { return x + z; }
    void fill_slot(int s, const gf::NpcSlotFill& f) override { slots[static_cast<std::size_t>(s)] = f; }
};

const gf::NpcSpawnRules kRules{24, 32};

gf::NpcSpawnRequest req(int chara, int flags) {
    gf::NpcSpawnRequest r;
    r.chara_id = chara;
    r.flags = flags;
    r.x = 100;
    r.z = 20;
    r.yaw = 512;
    r.state_step = 3;
    return r;
}

}  // namespace

SH_TEST(npc_spawn_group_spawns_once) {
    Slots s;
    SH_CHECK(gf::spawn_npc(s, kRules, req(5, 3)) == 0);
    SH_CHECK(s.slots[0].chara_id == 5 && s.slots[0].spawn_group == 3 && s.slots[0].y == 120);
    SH_CHECK(s.mask == (1u << 3));
    // The same group again answers the slot holding it.
    SH_CHECK(gf::spawn_npc(s, kRules, req(5, 3)) == 0);
    SH_CHECK(s.slots[1].chara_id == 0);
    // A non-group character takes the next free slot whatever its flags.
    SH_CHECK(gf::spawn_npc(s, kRules, req(30, 3)) == 1);
    SH_CHECK(s.slots[1].spawn_group == 0);
}

SH_TEST(npc_spawn_room_cap_and_full) {
    Slots s;
    s.cap = 1;
    SH_CHECK(gf::spawn_npc(s, kRules, req(5, 1)) == 0);
    SH_CHECK(gf::spawn_npc(s, kRules, req(6, 2)) == 0);  // the cap answers 0, nothing spawned
    SH_CHECK(s.slots[1].chara_id == 0 && s.mask == (1u << 1));
    for (auto& f : s.slots) {
        f.chara_id = 40;
    }
    SH_CHECK(gf::spawn_npc(s, kRules, req(30, 0)) == 32);  // no free slot
}

SH_TEST(npc_spawn_release_frees_group) {
    Slots s;
    gf::spawn_npc(s, kRules, req(5, 7));
    gf::release_npc_group(s, kRules, 5, 7);
    SH_CHECK(s.mask == 0);
    s.mask = 1u << 7;
    gf::release_npc_group(s, kRules, 30, 7);  // not a group character: the bit stays
    SH_CHECK(s.mask == (1u << 7));
}
