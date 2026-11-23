// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Shared between the files of the tools module (src/features/tools): the viewer settings, the editor's enter and
// leave, and the few engine settings the viewer borrows while the editor is open (tools_engine.cpp).

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "formats/common/status.h"

namespace sh::tools {

// The viewer panel's choices. Applied only while the editor is open; written to
// config.cfg (editor_* keys) only by the panel's "Save viewer settings".
struct ViewerSettings {
    bool fog_off = true;
    bool full_bright = false;
    float light_scale = 1.0f;
    bool flashlight_off = false;
    float brightness = 1.0f;  // multiplies the game's brightness grade
    float contrast = 1.0f;
    bool wireframe = false;
    bool collision_view = false;
};
ViewerSettings& viewer();
void load_viewer_settings();
void save_viewer_settings();

// The editor itself (tools_host.cpp).
bool tool_mode_active();
void enter_editor();
void leave_editor();
// Leaves the editor, warps to the map and opens the editor again when the map is in play.
void warp_to_map(const std::string& map);

// The places of the loaded map to step through (map_overlays.cpp): one map point per room where the map has
// rooms, else every map point. `room` is 0 without rooms.
struct RoomStop {
    int point = 0;
    int room = 0;
    double x = 0, z = 0;  // metres
};
std::vector<RoomStop> room_stops();
// The stop the player stands in or nearest to (-1 without stops).
int current_room_stop(const std::vector<RoomStop>& stops);
// The player onto the stop, the camera behind them.
void go_to_room_stop(const RoomStop& stop);
// Once a frame while a room stop settles: the player is held on it until its floor has loaded.
void room_stop_tick();
// The player has no floor under him (a map point only a scene uses): he goes to the first room stop that has one.
void stand_on_floor();
// The next presented frame to a PNG, with or without the tool UI.
void request_shot(const std::string& path, bool with_ui);
bool set_overlay_enabled(const std::string& id, bool on);
bool overlay_enabled(const std::string& id);

// Opens <root>/<rel> of the mod `mod` in the Lua editor panel (lua_editor.cpp); the mod need not be running.
Result<void> open_in_lua_editor(const std::filesystem::path& root, const std::string& rel, const std::string& mod);

// The built-in panels (viewer, navigation, coordinates), registered through tools.h like any other.
void register_builtin_panels();

// --- tools_engine.cpp: the engine's settings and commands the editor borrows ---
bool tools_enabled();  // config.cfg modding_tools (default 0; Options > Interface)
const char* config_value(const char* key);
bool world_fog_enabled();
void set_world_fog_enabled(bool on);
int fog_debug_disabled();
void set_fog_debug_disabled(int value);
void draw_distance(int* pct, int* whole_map);
void set_draw_distance(int pct, int whole_map);
bool collision_view_enabled();
void set_collision_view(bool on);
void run_command_now(const std::string& line);

}  // namespace sh::tools
