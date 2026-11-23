// SPDX-License-Identifier: GPL-3.0-or-later
//
// The map start order on the session (sh::gameflow::MapStart): its host over the
// player, the message box, the file queue and the systems the game view reaches. The map load
// calls sh::app::view::npc_init and in_game_init.

#include "engine/app/game_view.h"
#include "engine/app/loader/game_files.h"
#include "engine/game_state/game_state.h"
#include "engine/gameflow/map_start.h"
#include "engine/player/player.h"
#include "engine/text/message_box_view.h"

namespace {

namespace view = sh::app::view;

// The first street: Harry's match goes out as the map starts.
constexpr const char* kMatchOutMap = "map0_s01";

class SessionMapStartHost final : public sh::gameflow::MapStartHost {
public:
    int map() const override { return sh::game_state::save_map_index(); }

    void camera_init() override { view::map_camera_init(); }
    void camera_warp() override { view::map_camera_warp(); }
    void collision_triggers_set() override { view::collision_triggers_set(); }
    void map_effects_set() override { view::map_effects_set(); }
    void map_effects_reset() override { view::map_effects_reset(); }
    void map_effects_init(int map) override { view::map_effects_init(map); }
    void chara_models_process_loads() override { view::chara_models_process_loads(); }
    void flashlight_fix() override { view::flashlight_fix(); }
    void environment_apply(int environment, int level) override { view::environment_apply(environment, level); }
    void particles_update(int a, int map, int b) override { view::particles_update(a, map, b); }
    void npcs_clear() override { view::npcs_clear(); }
    void messages_init() override { sh::text::init_message_box(); }
    void player_map_init() override { sh::game::player().enter_map(sh::game_state::save_map_index() == sh::game_state::map_find(kMatchOutMap)); }
    void npcs_spawn_room() override { view::npcs_spawn_room(); }
    void player_height_update() override { view::player_height_update(); }
    void anim_bones_update() override { view::anim_bones_update(); }
    void weapon_info_update() override { sh::game::player().update_weapon(); }
    void common_texture_load() override { view::common_texture_load(); }
    void wait_files() override { sh::loader::queue_wait_empty(); }
    void map_items_models_load(int map) override { view::map_items_models_load(map); }
};

sh::gameflow::MapStart& map_start()
{
    static SessionMapStartHost host;
    static sh::gameflow::MapStart start(host);
    return start;
}

}  // namespace

void sh::app::view::npc_init() { map_start().npc_init(); }

void sh::app::view::in_game_init() { map_start().in_game_init(); }
