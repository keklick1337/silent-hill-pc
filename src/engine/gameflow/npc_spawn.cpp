#include "engine/gameflow/npc_spawn.h"

namespace sh::gameflow {

namespace {

constexpr int kGroupBits = 0x1F;
constexpr int kGroupFlagsLimit = 64;

bool group_chara(const NpcSpawnRules& rules, int chara_id) { return chara_id <= rules.last_group_chara; }

}  // namespace

int spawn_npc(NpcSpawnHost& host, const NpcSpawnRules& rules, const NpcSpawnRequest& request) {
    const bool grouped = group_chara(rules, request.chara_id);
    const bool group_flags = grouped && request.flags < kGroupFlagsLimit;
    const int group = group_flags ? request.flags & kGroupBits : 0;

    if (grouped) {
        if (host.group_spawned(group)) {
            // Already spawned this session: answer the slot that holds the group (empty slots count too).
            for (int i = 0; i < rules.room_slots; ++i) {
                if (host.slot_group(i) == group) {
                    return i;
                }
            }
            return rules.room_slots;
        }
        int live = 0;
        for (int i = 0; i < rules.room_slots; ++i) {
            if (host.slot_chara(i) != 0) {
                ++live;
            }
        }
        if (live >= host.room_cap()) {
            return 0;
        }
    }

    for (int i = 0; i < rules.room_slots; ++i) {
        if (host.slot_chara(i) != 0) {
            continue;
        }
        if (group_flags) {
            host.set_group_spawned(group, true);
        }
        NpcSlotFill fill;
        fill.chara_id = request.chara_id;
        fill.spawn_group = group;
        fill.state_step = request.state_step;
        fill.x = request.x;
        fill.z = request.z;
        fill.y = host.ground_height(request.x, request.z);
        fill.yaw = request.yaw;
        host.fill_slot(i, fill);
        return i;
    }
    return rules.room_slots;
}

void release_npc_group(NpcSpawnHost& host, const NpcSpawnRules& rules, int chara_id, int spawn_group) {
    if (group_chara(rules, chara_id) && static_cast<unsigned>(spawn_group) < static_cast<unsigned>(rules.room_slots)) {
        host.set_group_spawned(spawn_group, false);
    }
}

}  // namespace sh::gameflow
