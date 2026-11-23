// The order of the map set-up steps and the conditional environment restore.

#include <string>
#include <vector>

#include "engine/gameflow/map_start.h"

#include "framework/sh_test.h"

namespace gf = sh::gameflow;

namespace {

class Host final : public gf::MapStartHost {
public:
    std::vector<std::string> log;
    bool pending = false;
    int map() const override { return 12; }
    bool environment_pending() const override { return pending; }
    int environment_set() const override { return 3; }
#define STEP(name) void name() override { log.push_back(#name); }
    STEP(camera_init) STEP(camera_warp) STEP(collision_triggers_set) STEP(map_effects_set) STEP(map_effects_reset)
    STEP(chara_models_process_loads) STEP(flashlight_fix) STEP(npcs_clear) STEP(messages_init) STEP(player_map_init)
    STEP(npcs_spawn_room) STEP(player_height_update) STEP(anim_bones_update) STEP(weapon_info_update)
    STEP(common_texture_load) STEP(wait_files)
#undef STEP
    void map_effects_init(int m) override { log.push_back("map_effects_init " + std::to_string(m)); }
    void environment_apply(int e, int l) override { log.push_back("env " + std::to_string(e) + " " + std::to_string(l)); }
    void particles_update(int a, int m, int b) override {
        log.push_back("particles " + std::to_string(a) + " " + std::to_string(m) + " " + std::to_string(b));
    }
    void npc_flags_id_set(int i) override { log.push_back("flags_id " + std::to_string(i)); }
    void map_items_models_load(int m) override { log.push_back("items " + std::to_string(m)); }
};

}  // namespace

SH_TEST(map_start_npc_init_order) {
    Host h;
    gf::MapStart(h).npc_init();
    SH_CHECK((h.log == std::vector<std::string>{"camera_warp", "map_effects_reset", "npcs_clear", "npcs_spawn_room",
                                                "player_height_update"}));
    h.log.clear();
    h.pending = true;
    gf::MapStart(h).npc_init();
    SH_CHECK(h.log[2] == "env 3 127" && h.log[3] == "particles 0 12 0" && h.log.size() == 7);
}

SH_TEST(map_start_in_game_init_order) {
    Host h;
    gf::MapStart(h).in_game_init();
    SH_CHECK((h.log == std::vector<std::string>{
                  "camera_init", "camera_warp", "collision_triggers_set", "map_effects_set", "chara_models_process_loads",
                  "flashlight_fix", "particles 0 12 -1", "npcs_clear", "flags_id 5", "map_effects_init 12", "messages_init",
                  "player_map_init", "npcs_spawn_room", "player_height_update", "anim_bones_update", "weapon_info_update",
                  "common_texture_load", "wait_files", "items 12"}));
}
