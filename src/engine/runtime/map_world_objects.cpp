// SPDX-License-Identifier: GPL-3.0-or-later
// The world object steps of the running map -- see map_world_objects.h.

#include "engine/runtime/map_world_objects.h"
#include "engine/loader/active_map.h"

#include "engine/maps/map_runtime.h"
#include "engine/runtime/world_object_runtime.h"

extern "C" {
// The killed-enemy word of the chosen spawn variant goes back to the variant (the map data's spawn variants).
void MapData_SpawnVariantsStateSave(void);
}

namespace sh::world {

void map_objects_init() {
    if (!sh::maps::map_runtime().has_world_objects()) {
        return;
    }
    const char* map = sh::loader::active_map_name();
    world_object_runtime().init(map != nullptr ? map : "");
    world_object_runtime().call("post_init", 0, 0, 0);
}

void map_objects_update() {
    if (!sh::maps::map_runtime().has_world_objects()) {
        return;
    }
    long long draw = 1;
    world_object_runtime().call_int("pre_draw", 0, 0, 0, &draw);
    if (draw != 0) {
        world_object_runtime().draw();
    }
    world_object_runtime().call("post_draw", 0, 0, 0);
    MapData_SpawnVariantsStateSave();
}

void map_objects_npc_spawn() { world_object_runtime().call("npc_spawn", 0, 0, 0); }

void map_objects_post_camera() { world_object_runtime().call("post_camera", 0, 0, 0); }

}  // namespace sh::world

// The map set-up step of the effect slots calls it by this name.
extern "C" void Map_WorldObjectsInit(void) { sh::world::map_objects_init(); }
