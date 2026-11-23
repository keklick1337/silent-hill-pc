// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/gameflow/world_frame.h"

#include <algorithm>

#include "formats/common/json.h"

namespace sh::gameflow {

Result<WorldFrameRules> WorldFrameRules::parse(std::string_view text) {
    const Result<json::Value> doc = json::parse(text);
    if (!doc) {
        return Result<WorldFrameRules>::fail("world_frame.json: " + doc.error());
    }
    WorldFrameRules out = builtin();
    if (const json::Value* maps = doc.value().find("maps_without_particles")) {
        if (!maps->is_array()) {
            return Result<WorldFrameRules>::fail("world_frame.json: \"maps_without_particles\" must be a list of map names");
        }
        out.maps_without_particles.clear();
        for (const json::Value& m : maps->array()) {
            if (!m.is_string() || m.string().empty()) {
                return Result<WorldFrameRules>::fail("world_frame.json: a map name is a non-empty string");
            }
            out.maps_without_particles.push_back(m.string());
        }
    }
    if (const json::Value* rumble = doc.value().find("health_rumble")) {
        if (!rumble->is_array() || rumble->array().size() > 11) {
            return Result<WorldFrameRules>::fail("world_frame.json: \"health_rumble\" must be a list of at most 11 values");
        }
        out.health_rumble.clear();
        for (const json::Value& v : rumble->array()) {
            if (!v.is_integer() || v.integer() < 0 || v.integer() > 255) {
                return Result<WorldFrameRules>::fail("world_frame.json: a rumble value is an integer from 0 to 255");
            }
            out.health_rumble.push_back(static_cast<std::int32_t>(v.integer()));
        }
    }
    return Result<WorldFrameRules>::ok(std::move(out));
}

std::int32_t WorldFrameRules::rumble_for(std::int32_t tenths) const {
    if (tenths < 0 || tenths >= static_cast<std::int32_t>(health_rumble.size())) {
        return -1;
    }
    return health_rumble[static_cast<std::size_t>(tenths)];
}

bool WorldFrameRules::particles_on(std::string_view map) const {
    return std::find(maps_without_particles.begin(), maps_without_particles.end(), map) == maps_without_particles.end();
}

void run_world_frame(WorldFrameHost& host, const WorldFrameRules& rules) {
    host.latch_chunks_ready();
    // A paused soundtrack (a cutscene hold) freezes the map objects and, below, the rest of the world.
    if (!host.music_paused()) {
        host.update_world_objects();
    }
    host.cutscene_camera();
    host.crosshair();
    host.music_track();
    host.restore_random();
    host.restore_random();
    if (host.music_paused()) {
        return;
    }

    host.collision_triggers();
    host.move_camera();
    host.alternate_cameras();
    host.map_frame_hook();
    host.restore_random();

    host.update_player();
    host.restore_random();
    host.update_flashlight();
    if (rules.particles_on(host.map_name())) {
        host.update_particles();
    }
    host.restore_random();

    if (host.player_visible()) {
        host.draw_player();
    }
    host.restore_random();

    host.npc_spawn_pass();
    host.update_npcs();
    host.update_2d_effects();
    host.close_range_chunks();
    host.sustain_positional_loops();
    host.draw_world();
    host.frame_world();
    host.advance_random();
}

}  // namespace sh::gameflow
