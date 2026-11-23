#include "engine/gameflow/map_start.h"

namespace sh::gameflow {

namespace {
constexpr int kNoValue = -1;
constexpr int kEnvironmentLevel = 127;
constexpr int kNpcFlagsAfterLoad = 5;
}  // namespace

void MapStart::npc_init() {
    host_.camera_warp();
    host_.map_effects_reset();
    if (host_.environment_pending()) {
        host_.environment_apply(host_.environment_set(), kEnvironmentLevel);
        host_.particles_update(0, host_.map(), 0);
    }
    host_.npcs_clear();
    host_.npcs_spawn_room();
    host_.player_height_update();
}

void MapStart::in_game_init() {
    const int map = host_.map();
    host_.camera_init();
    host_.camera_warp();
    host_.collision_triggers_set();
    host_.map_effects_set();
    host_.chara_models_process_loads();
    host_.flashlight_fix();
    host_.particles_update(0, map, kNoValue);
    host_.npcs_clear();
    host_.npc_flags_id_set(kNpcFlagsAfterLoad);
    host_.map_effects_init(map);
    host_.messages_init();
    host_.player_map_init();
    host_.npcs_spawn_room();
    host_.player_height_update();
    host_.anim_bones_update();
    host_.weapon_info_update();
    host_.common_texture_load();
    host_.wait_files();
    host_.map_items_models_load(map);
}

}  // namespace sh::gameflow
