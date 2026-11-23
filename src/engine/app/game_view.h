// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the engine's boot, main loop and demo hosts (src/engine/app) need of the running game: one function each, in
// src/engine/app/game_view.cpp over the game's entry points by name, except the parts that need the frame layers and
// the display (src/engine/app/game_view_frame.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sh::loader {
struct ImagePlace;
}

namespace sh::app::view {

// --- demo ---
// The map overlay's memory (the demo's play buffer must stay above the overlay of its map).
std::uintptr_t map_overlay_base();
// A world position is near a loaded chunk cell (enemies spawn in a demo only there).
bool near_active_cell(std::int32_t x, std::int32_t z);

// --- pictures the boot and the title screens read into their homes ---
enum class Picture { Font16Atlas, KonamiLogo, KcetLogo, ItemInspection };
const sh::loader::ImagePlace* picture(Picture which);
// The boot's resident world loads: Harry's model, the item and effect textures of the world (all four, or one).
void intro_world_loads();
void harry_model_load();
void item_textures_load();
void world_effect_textures_load();
void map_effect_textures_load();

// --- screen and sound ---
void screen_init();
// The display in `width` pixels, interlaced or not.
void screen_init_mode(int width, bool interlaced);
// Clears an area of both interlaced fields to black.
void clear_screen_rect(int x, int y, int w, int h);
void screen_position_set(int x, int y);
void audio_init();
bool audio_streaming();
void audio_tasks_run();
void audio_tasks_drain();
void audio_work_init();
void ambient_set(int mode);
void vibration_play(int effect);
void vibration_update();
void vibration_mode_request(int mode);

// --- the world ---
void water_update();
bool fog_enabled();
void set_fog_enabled(bool on);
void fog_color(std::uint8_t rgb[3]);
bool player_control_disabled();
// The no-target cheat: the attack-veto flag set and its timer held at zero, so it cannot expire.
void hold_no_target();
// Message `index` of the loaded map's message table.
const char* map_message(std::int32_t index);
// "HARRY POSITION LOGGED": the player's place, ground and the camera in one journal line.
void log_player_position(bool free_camera);

// --- free camera ---
// Harry and his held item visible again (the free camera hides nothing itself; scenes may have).
void player_show();
// The game's view from `eye` toward `at` (Q12 world positions).
void view_look_at(const std::int32_t eye[3], const std::int32_t at[3]);

// --- the frame (order tables and packets) ---
void geometry_init();
// The frame's buffer index (the display double buffer being built).
int frame_buffer_begin();
// The packet area of this frame starts at `packets`; both order tables are cleared.
void frame_lists_clear(void* packets);
std::ptrdiff_t packets_used(const void* base);
void sort_clear(std::uint8_t r, std::uint8_t g, std::uint8_t b);
// The world layer's draw-area changes that send the scene offscreen and back become scene captures.
void prepare_world_list();
void draw_world_list();
void draw_ui_list();
void swap_buffers();
void set_projection_offset(std::int32_t x, std::int32_t y);
void item_depth_begin();
void item_depth_end();

// --- map load (sh::gameflow::MapLoad's host) ---
// The running map event's flag bits (world frozen, to-play, no ambient wait).
int map_event_flags();
void area_player_position_update();
void player_init();
void map_load(int map_index);
// The map's loading screen `screen` (its overlay's table) for this frame.
void loading_screen_run(int screen);
void background_motion_blur();
void chara_anim_alloc(int slot, int chara_id);
void map_chara_groups_load();
void world_clear();
void player_chunk_init();
bool chunks_ready();
int music_init();
// The load watchdog: the XA voice index the sound driver waits on, cleared by force.
void audio_state_force_clear();
int ambient_init();
void npc_init();
void in_game_init();
void transition_sound();
// Frames the load flag has been up (an 8-bit count), after counting this one.
int load_flag_tick();

// --- map start (sh::gameflow::MapStart's host) ---
void map_camera_init();
// The camera snapped to the player at the script camera's yaw.
void map_camera_warp();
void collision_triggers_set();
void map_effects_set();
void map_effects_reset();
void map_effects_init(int map_index);
void chara_models_process_loads();
void flashlight_fix();
void environment_apply(int environment, int level);
void particles_update(int a, int map_index, int b);
// Every NPC slot, its flags and the group flags emptied.
void npcs_clear();
void npcs_spawn_room();
void player_height_update();
void anim_bones_update();
void common_texture_load();
void map_items_models_load(int map_index);

// --- game states ---
// The update of a top-level game state by its data name (gamedata/config/boot.json), or null.
using StateUpdate = void (*)();
StateUpdate game_state_update(std::string_view name);

}  // namespace sh::app::view
