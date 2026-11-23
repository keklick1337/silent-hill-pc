#pragma once

// The loading screen's running player, one frame. The frame owns the
// order: the one-time setup when the system state first reaches gameplay (camera, targets, pose), then the
// per-frame light and flashlight reset, the run animation on the screen's own clock and the draw. The
// systems are reached through LoadScreenHost; the numbers are load_screen_pose.h.

#include <cstdint>

#include "engine/gameflow/load_screen_pose.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

class LoadScreenHost {
public:
    virtual ~LoadScreenHost() = default;

    virtual int state_is_gameplay() const { return sh::game::session().sys_state() == sh::game::sys_state::kGameplay; }
    virtual int overlay_transition() const { return sh::game::session().system.process.process_flags == sh::game::process_flag::kOverlayTransition; }
    virtual void area_player_position_update() = 0;
    virtual void camera_init() = 0;
    virtual void collision_triggers_set() = 0;
    virtual void player_placement(std::int32_t& x, std::int32_t& z, std::int32_t& yaw) const = 0;
    virtual void camera_watch_target(const LoadScreenTarget& t) = 0;
    virtual void camera_position_target(const LoadScreenTarget& t) = 0;
    virtual void spotlight_attribs_fix() = 0;
    virtual void map_effects_update() = 0;
    virtual void player_pose(std::int32_t anim_time, std::int32_t height) = 0;  // visible, unlocked, bones enabled
    virtual void player_status_save() = 0;
    virtual void player_matrix_set() = 0;
    virtual void state_advance() { ++sh::game::session().system.flow.sys_state; }

    virtual void player_anim_flags_set() = 0;  // every frame: unlocked and visible, no disabled bones
    virtual void bones_reset() = 0;            // forces the hierarchy to be recomputed
    virtual void neutral_light_set(std::int32_t tint) = 0;
    virtual void fog_off() = 0;
    virtual void flashlight_pass_off() = 0;
    virtual std::int32_t screen_dt() const = 0;
    virtual void anim_run(std::int32_t dt) = 0;
    virtual void camera_move() = 0;
    virtual void flashlight_update() = 0;
    virtual void player_draw() = 0;
};

void load_screen_player_frame(LoadScreenHost& host, const LoadScreenParams& params);

}  // namespace sh::gameflow
