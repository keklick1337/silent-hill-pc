// The collision groups and collision triggers as C entry points of the game code:
// the active-trigger record is the engine's
// (sh::world::active_triggers()), read through Collision_ActiveTriggersGet.

#include <cstdint>

#include "engine/game_state/session.h"
#include "engine/world/collision_triggers.h"
#include "engine/world/mover_setup.h"

namespace wd = ::sh::world;

extern "C" {

wd::ActiveTriggers* Collision_ActiveTriggersGet(void) {
    return &wd::active_triggers();
}

void Collision_Init(void) {
    wd::ActiveTriggers& a = wd::active_triggers();
    a.groups = wd::kMapGeometryGroup;
    a.count = 0;
}

std::uint16_t Collision_FlagsGet(void) {
    return wd::active_triggers().groups;
}

void Collision_FlagsSet(std::uint16_t collFlags) {
    wd::active_triggers().groups = collFlags;
}

void Collision_FlagBitsSet(std::uint16_t collFlags) {
    wd::active_triggers().groups |= collFlags;
}

void Collision_FlagBitsClear(std::int32_t collFlags) {
    wd::ActiveTriggers& a = wd::active_triggers();
    a.groups = wd::clear_groups(a.groups, collFlags);
}

void Collision_NearbyTriggersGet(std::int32_t posX, std::int32_t posZ, const wd::PackedTrigger* triggers) {
    wd::find_nearby_triggers(wd::active_triggers(), posX, posZ, triggers);
}

}  // extern "C"
