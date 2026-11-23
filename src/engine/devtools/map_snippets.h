// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// One record of a map's data written the way its gamedata file spells it: an
// events.json, points.json, spawns.json slot, cameras.json, collision.json or world_objects.json entry, and the
// events.lua function of a handler. The editor's map inspector copies these; the key order and units are the ones
// the readers take. Fixed-point values that the file writes in
// metres or degrees beside an integer are written exactly (q / 4096 and q * 360 / 256 are exact doubles), so the
// two can never disagree; world_objects.json has no integer beside its metres, so they are the shortest decimal
// that reads back (position_snippets.h).

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sh::devtools {

// A JSON number of a double the way the data files write it (shortest round trip; integers without a point).
std::string json_number(double value);
// A JSON string literal (quotes and escapes).
std::string json_string(const std::string& text);

// The shortest decimal degrees (at most 6 decimals) that world_objects.json reads back as `angle` (Q12 turns:
// (s16)(s32)((float)deg / 360 * 4096)); with more, the nearest one.
std::string degrees_text(std::int16_t angle);

// An events.json entry. A flag or item is its name ("" or "0": none); `map` names a door's target map (sys
// state 5), else map_idx is written as a number.
struct EventEntry {
    int index = 0;
    std::string required_flag;
    std::string complete_flag;
    int trigger_type = 0;
    int activation_type = 0;
    int map_point = 0;
    std::string required_item;
    int sys_state = 0;
    int param = 0;
    int transition_flags = 0;
    int sfx_pair = 0;
    int camera_rail = 0;
    int map_idx = 0;
    std::string map;
};
std::string event_json(const EventEntry& e);

// A points.json entry.
struct PointEntry {
    int index = 0;
    std::int32_t x_q12 = 0, z_q12 = 0;
    int paper_map = 0;
    int loading_screen = 0;
    int heading_q8 = 0;
    int half_width_q4 = 0;
    int weather_on_enter = 0;
};
std::string point_json(const PointEntry& p);

// A slot of spawns.json "groups[g].slots" (chara: the slot's own name, "None" for the group's character).
struct SpawnEntry {
    std::int32_t x_q12 = 0, z_q12 = 0;
    int rotation_q8 = 0;
    std::string chara;
    int flags = 0;
    int difficulty_min = 0;
};
std::string spawn_json(const SpawnEntry& s);
// The event-script form: sh.chara_spawn(chara, 0, x, z, rot, step) with the slot's rotation in Q12 turns and
// its entry state as the step.
std::string spawn_lua(const SpawnEntry& s, const std::string& chara);

// A cameras.json entry; areas are [min_x, max_x, min_z, max_z] in Q4 (16 = 1 m), heights Q4.
struct CameraEntry {
    int index = 0;
    std::array<int, 4> switch_area_q4{};
    std::array<int, 4> road_area_q4{};
    int flags = 0;
    int area_size = 0;
    int road_type = 0;
    int move_y = 0;
    int camera_move = 0;
    int road_max_y_q4 = 0, road_min_y_q4 = 0, watch_offset_y_q4 = 0;
    int lens_flare = 0;
    int fix_angle_x = 0, fix_angle_y = 0;
};
std::string camera_json(const CameraEntry& c);

// A collision.json trigger (whole metres, height in half metres).
struct CollisionEntry {
    int index = 0;
    int x = 0, z = 0, size_x = 0, size_z = 0, height_half_m = 0;
};
std::string collision_json(const CollisionEntry& c);

// A world_objects.json object. Without a pose the position (Q12) and, when not zero, the rotation are written;
// with one, "pose" stands for both.
struct WorldObjectEntry {
    std::string name;
    std::string model;
    std::array<std::int32_t, 3> position{};
    std::array<std::int16_t, 3> rotation{};
    std::vector<std::pair<std::vector<int>, std::vector<int>>> zones;  // (x cells, z cells); empty axis: any
    std::vector<std::string> flags_set;
    std::vector<std::string> flags_clear;
    bool script = false;
    bool pc_near_pickup = false;
    std::string item_model;
    double item_model_scale = 1.0;
    std::string pose_table;  // non-empty: "pose": {"disc_table", "index"}
    int pose_index = 0;
};
std::string world_object_json(const WorldObjectEntry& o);

// The events.lua global of a handler slot: `function <name>()` with a comment naming the slot.
std::string handler_lua(const std::string& name, int slot, const std::string& shape);

}  // namespace sh::devtools
