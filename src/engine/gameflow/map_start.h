#pragma once

// What happens when a map's play state is set up: the order of
// the steps that bring the camera, collision triggers, effects, the player and the map's characters
// to a playable state, and the reset of the character slots. The steps themselves belong to other
// systems and are reached through MapStartHost; this class owns only the order and the few
// conditions between steps.

#include "engine/game_state/session.h"

namespace sh::gameflow {

class MapStartHost {
public:
    virtual ~MapStartHost() = default;

    virtual int map() const = 0;                     // map number of the save
    // the previous map left an environment to restore
    virtual bool environment_pending() const { return sh::game::session().system.particles.transition_ready != 0; }
    // that environment
    virtual int environment_set() const { return sh::game::session().system.particles.density_mode; }

    virtual void camera_init() = 0;                  // camera of the map, at the player
    virtual void camera_warp() = 0;                  // snap the camera to the player's position and facing
    virtual void collision_triggers_set() = 0;
    virtual void map_effects_set() = 0;              // fog, light and sky of the map
    virtual void map_effects_reset() = 0;            // forget running effect states
    virtual void map_effects_init(int map) = 0;
    virtual void chara_models_process_loads() = 0;
    virtual void flashlight_fix() = 0;
    virtual void environment_apply(int environment, int level) = 0;
    virtual void particles_update(int a, int map, int b) = 0;
    virtual void npcs_clear() = 0;                   // empty every NPC slot and its flags
    virtual void npc_flags_id_set(int id) { sh::game::session().system.scene.npc_flags_owner = id; }
    virtual void messages_init() = 0;
    virtual void player_map_init() = 0;
    virtual void npcs_spawn_room() = 0;
    virtual void player_height_update() = 0;
    virtual void anim_bones_update() = 0;
    virtual void weapon_info_update() = 0;
    virtual void common_texture_load() = 0;
    virtual void wait_files() = 0;                   // blocks until the file queue is empty
    virtual void map_items_models_load(int map) = 0;
};

class MapStart {
public:
    explicit MapStart(MapStartHost& host) : host_(host) {}

    // A room reload that keeps the map: camera, environment restore, character reset and respawn.
    void npc_init();
    // The full set-up after a map load.
    void in_game_init();

private:
    MapStartHost& host_;
};

}  // namespace sh::gameflow
