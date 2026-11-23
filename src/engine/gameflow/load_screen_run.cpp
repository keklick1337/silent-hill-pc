#include "engine/gameflow/load_screen_run.h"

namespace sh::gameflow {

void load_screen_player_frame(LoadScreenHost& h, const LoadScreenParams& p) {
    if (h.state_is_gameplay()) {
        if (h.overlay_transition()) h.area_player_position_update();
        h.camera_init();
        h.collision_triggers_set();
        std::int32_t x = 0, z = 0, yaw = 0;
        h.player_placement(x, z, yaw);
        const LoadScreenView view = load_screen_view(p, x, z, yaw);
        h.camera_watch_target(view.look_at);
        h.camera_position_target(view.cam_target);
        h.spotlight_attribs_fix();
        h.map_effects_update();
        h.player_pose(p.pose_time, p.player_height);
        h.player_status_save();
        h.player_matrix_set();
        h.state_advance();
    }
    h.player_anim_flags_set();
    h.neutral_light_set(p.light_tint);
    h.fog_off();
    h.flashlight_pass_off();
    h.bones_reset();
    h.anim_run(h.screen_dt());
    h.camera_move();
    h.flashlight_update();
    h.player_draw();
}

}  // namespace sh::gameflow
