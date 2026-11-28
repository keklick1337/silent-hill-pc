// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The world update of an in-game frame: the order in which the map objects, the
// camera, the player, the particles, the NPCs and the world draw run after the frame's game state, where a
// paused soundtrack stops the update, and the pad rumble of a hurt player. The host runs each step over the
// game's records.
//
// Numbers are gamedata/config/world_frame.json; a mod replaces the file whole.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "formats/common/status.h"

#include "engine/game_state/session.h"

namespace sh::gameflow {

struct WorldFrameRules {
    // Maps whose particle update the frame skips (their effects run their own).
    std::vector<std::string> maps_without_particles{"map7_s03"};
    // Pad rumble while hurt, by health rounded up to tenths (index 0..); higher health leaves it unchanged.
    std::vector<std::int32_t> health_rumble{17, 16, 16, 15, 14, 13, 12};

    static WorldFrameRules builtin() { return {}; }
    static Result<WorldFrameRules> parse(std::string_view json);

    // The rumble for health at `tenths`, or -1 to leave it.
    std::int32_t rumble_for(std::int32_t tenths) const;
    bool particles_on(std::string_view map) const;
};

class WorldFrameHost {
public:
    virtual ~WorldFrameHost() = default;
    // Latches "the fade is done and the queued chunks are in" for the map code.
    virtual void latch_chunks_ready() = 0;
    virtual bool music_paused() { return sh::game::session().music_status(sh::game::music_status::kPause); }
    virtual void update_world_objects() = 0;
    virtual void cutscene_camera() = 0;
    virtual void crosshair() = 0;
    virtual void music_track() = 0;
    // The demo's random seed, restored before each step that may draw from it.
    virtual void restore_random() = 0;
    virtual void collision_triggers() = 0;
    virtual void move_camera() = 0;
    // TPS/FPS and the free cam replace the view the camera just published, before any map hook bakes it.
    virtual void alternate_cameras() = 0;
    virtual void map_frame_hook() = 0;
    virtual void update_player() = 0;
    virtual void update_flashlight() = 0;
    virtual std::string map_name() = 0;
    virtual void update_particles() = 0;
    virtual bool player_visible() = 0;
    virtual void draw_player() = 0;
    virtual void npc_spawn_pass() = 0;
    virtual void update_npcs() = 0;
    virtual void update_2d_effects() = 0;
    virtual void close_range_chunks() = 0;
    // Every map and event caller has written its positional sounds by now.
    virtual void sustain_positional_loops() = 0;
    virtual void draw_world() = 0;
    // The world is in this frame's draw list: pick its framing where it is submitted.
    virtual void frame_world() = 0;
    virtual void advance_random() = 0;
};

void run_world_frame(WorldFrameHost& host, const WorldFrameRules& rules);

}  // namespace sh::gameflow
