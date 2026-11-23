// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The state dump of the debug console (`dump`): a snapshot of the running game a bug report is
// handed over with. This is the document itself: what a dump holds, its id and its JSON; the feature that fills it
// from the live game and writes the folder is src/features/dump.

#pragma once

#include "engine/devtools/perf_monitor.h"
#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sh::devtools {

// A world position, Q12 (4096 = 1 m). The JSON carries both the raw value and metres.
struct DumpPoint {
    std::int32_t x = 0, y = 0, z = 0;
};

struct DumpCharacter {
    int slot = 0;  // 0 is the player, NPC slots follow
    std::string name;
    DumpPoint position;
    int yaw = 0;              // Q12 turn (4096 = 360 degrees)
    std::int32_t health = 0;  // Q12
    int control_state = 0, control_step = 0;
    std::vector<std::string> flags;  // names of the character flags that are set
    bool model_loaded = false;
    bool in_camera_frustum = false;
};

struct DumpObject {
    std::string name;
    std::string model;
    std::string item_model;
    DumpPoint position;
    std::array<int, 3> rotation{};  // Q12 turns
    bool pose_resolved = false;
    bool queued = false;   // handed to the object queue by the last draw
    std::string library;   // where the drawer found the model: "item", "global", "cell <x>,<z>", "none"
    int library_slot = -1; // the resident cell slot of a cell library
};

struct DumpCell {
    int slot = 0;
    int x = 0, z = 0;
    std::string file;
    bool ready = false;
};

struct DumpEffect {
    int slot = 0;
    int kind = 0;
    DumpPoint position;
};

struct DumpItem {
    std::string name;
    int count = 0;
};

// Where the time goes: the history of perf_monitor() and what the system tells at the moment of the dump. A negative
// number: the platform or the driver does not report it.
struct DumpPerformance {
    PerfReport history;
    double cpu_percent = -1.0;          // of one core, all threads: several busy threads read above 100
    double main_thread_percent = -1.0;  // the main thread is the render thread too
    int gpu_percent = -1;               // the adapter's load, every program on it
    double ram_mb = -1.0;
    double vram_game_mb = -1.0;         // the engine's own textures and buffers
    double vram_used_mb = -1.0, vram_total_mb = -1.0;  // the whole adapter
    int vsync = 0;                      // the config's keys: 0 off, 1 on, -1 adaptive
    int fps_cap = 0;                    // 0: no cap
    int refresh_rate = 0;
    bool fps_unlocked = false;          // the debug switch that lifts the cap
    int characters = 0;
    int remains_bodies = 0, remains_stains = 0;  // kept on this map
    int remains_bodies_all_maps = 0;
    int effect_slots = 0;
    int textures_loaded = -1;
    int texture_uploads_pending = -1;
};

struct StateDump {
    std::string time;      // local wall clock, ISO 8601
    std::string revision;  // the build's git revision, empty when unknown
    std::string note;

    std::string map;
    std::string map_description;
    int room = 0;
    std::string game_state, system_state;
    bool free_gameplay = false;  // the player is in control (no menu, event or transition)
    bool state_saved = false;    // state.sav was written

    // The player.
    DumpPoint position;
    int yaw = 0;
    std::int32_t health = 0;
    bool aiming = false;
    std::string weapon;  // the equipped weapon's id, empty: none
    int weapon_attack = 0;
    int ammo = 0;
    int anim_status = 0, anim_keyframe = 0, anim_flags = 0;
    int control_state = 0, control_step = 0, upper_body_state = 0;

    // The camera.
    DumpPoint camera_position, camera_look_at;
    int camera_pitch = 0, camera_yaw = 0, camera_roll = 0;  // Q12 turns
    int projection_distance = 0;                            // the perspective distance H, in screen units
    int projection_offset_x = 0, projection_offset_y = 0;
    int half_angle_x = 0, half_angle_y = 0;                 // Q12 turns, half of the view angles
    std::uint32_t camera_flags = 0;
    bool script_camera = false;   // a script owns the camera position (a fixed shot)
    bool script_look_at = false;  // a script owns the look-at point
    bool free_camera = false;
    int camera_zone = -1;         // the camera zone of the map the rig follows, -1: the default one
    std::array<std::int32_t, 9> view_rotation{};  // world -> view, Q12, row major
    DumpPoint view_origin;

    // The world.
    bool flashlight_on = false, flashlight_unavailable = false;
    int fog_mode = 0;
    std::int32_t fog_distance = 0;  // Q12
    std::array<int, 3> fog_color{};
    int light_mode = 0, light_level_max = 0;
    int spot_light_intensity = 0, world_light_intensity = 0;
    std::vector<std::string> flags;  // every event flag that is set, by name
    std::vector<DumpItem> inventory;
    std::vector<DumpCharacter> characters;
    std::vector<DumpObject> objects;
    bool exterior = false;
    std::string library_file;
    std::vector<DumpCell> cells;
    std::vector<DumpEffect> effects;

    // The build's settings that change the picture.
    std::string graphics_preset, renderer;
    int window_width = 0, window_height = 0, frame_width = 0, frame_height = 0;
    int widescreen_mode = 0;
    double render_scale = 1.0;
    bool native_resolution = false;

    DumpPerformance performance;

    std::vector<std::string> log;  // the newest lines of the journal, oldest first
};

// Eight hex digits from the dump's content and the time it was taken (FNV-1a, folded).
std::string state_dump_id(std::string_view content, std::uint64_t time);
bool valid_dump_id(std::string_view id);

// The dump as dump.json. Characters and objects carry their distance to the player; the objects are written nearest
// first.
std::string state_dump_json(const StateDump& dump, std::string_view id);

// What `dump load` reads back from dump.json.
struct DumpRestore {
    std::string map;
    DumpPoint position;
    int yaw = 0;
    bool state_saved = false;
};
Result<DumpRestore> parse_state_dump(std::string_view json_text);

}  // namespace sh::devtools
