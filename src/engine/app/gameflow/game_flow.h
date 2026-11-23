// SPDX-License-Identifier: GPL-3.0-or-later
//
// The game's boot-time services on the session: the title screen's files, the player's settings and the warm reset
// (sh::gameflow::WarmBoot).
#pragma once

namespace sh::app {

// The title picture (the style module picks which of the three and where from) and its seek.
void title_graphics_seek();
void title_graphics_load();
// The movie overlay, the options screen's and the save screen's picture and code.
void stream_overlay_seek();
void stream_overlay_load();
void option_overlay_load();
void save_overlay_load();

// The settings' screen position and sound volumes to the display and the sound driver.
void settings_apply_screen_and_volume();
// Default settings (layout A of the controller, full volumes, vibration on).
void settings_restore_defaults();
// The controller binds of layout `layout` (0..2) from gamedata/config/controller_layouts.json.
void settings_restore_control_defaults(int layout);

// Back to the title (pause, messages, sound, file queue, demo and the session reset).
void warm_boot();

// A new save record for a game that starts on map `map` at `difficulty` (src/engine/app/gameflow/game_boot_host.cpp).
void savegame_initialize(int map, int difficulty);
// The game's start after the title: the first map load (src/engine/app/gameflow/map_load_host.cpp).
void game_startup();

}  // namespace sh::app
