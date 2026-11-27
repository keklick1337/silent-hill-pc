#pragma once

// The narrow link of the game flow hosts (src/engine/gameflow/game, src/engine/entities/game) to the rest of the game:
// the session's character slots and running save, the map's event list and points, the file queue, the screen, the
// sound driver, the pads and the PC features. Implemented in the engine by game_link.cpp (over the session and the
// engine modules, plus narrow C calls into other modules); the last section, over the running
// map, the animation slots, the pictures and video memory, is game_link_records.cpp (
// section 25). Engine types only: no game record crosses this line except as an opaque pointer.

#include <cstdint>
#include <functional>
#include <string_view>

#include "engine/gameflow/event_commands.h"
#include "engine/gameflow/event_composites.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/gameflow/npc_spawn.h"
#include "render/draw2d/draw2d.h"

namespace sh::gameflow::game {

// --- Numbers of the game the hosts share ---
inline constexpr std::int32_t kTimestep60Fps = 68;   // Q12 1/60 s
inline constexpr std::int32_t kTimestep30Fps = 136;  // Q12 1/30 s
inline constexpr int kDefaultMessageLength = 99;
inline constexpr std::uint32_t kMusicVoiceDialog = 1u << 5;
inline constexpr std::int32_t kAnimPlaybackEnd = 1;
constexpr int screen_position_x(float percent) { return static_cast<int>(320 * (percent / 100.0f)); }
constexpr int screen_position_y(float percent) { return static_cast<int>(240 * (percent / 100.0f)); }

// --- Data, names and files ---
const char* gamedata_path();
int sound_number(const char* name);
int flag_number(const char* name);
int item_number(const char* name);
int common_pickup_number(const char* name);
int paper_map_number(const char* name);
std::int32_t find_image_file(const char* name);  // a TIM of the file table, -1 if none

// --- The map overlay: its event list, map points and hooks ---
bool map_events_present();
bool map_points_present();
int map_event_count(int max);                 // entries before the end marker, at most `max`
void* map_event_record(int index);            // the event's record in the overlay's list
int map_event_index(const void* record, int count);  // -1 when it is not one of the first `count`
struct EventInfo {
    std::int16_t required_flag = 0;
    std::int16_t done_flag = 0;
    int trigger_type = 0;
    int activation = 0;
    int point = 0;
    int item = 0;
    int sys_state = 0;
    int param = 0;
    int options = 0;
    int sfx_pair = 0;
    bool camera_rail = false;
    int map = 0;
};
EventInfo map_event(const void* record);
MapPointRecord map_point(int index);
void overlay_freeze_player();
void overlay_unfreeze_player();
bool overlay_player_stopped();
void overlay_update_world_objects();
void overlay_frame_hook();
void overlay_npc_spawn_event();
void overlay_particles_update(int map);
bool overlay_player_path(std::int32_t anim, std::int32_t* waypoints, std::int16_t heading, std::int32_t count);
enum class AnimOp : std::uint8_t { SetState, PlaybackState, Lock, Unlock, Reset };
std::int32_t overlay_anim(AnimOp op, void* chara, bool player, std::int32_t state);
void map_room_update();
bool map_chunks_idle();
void close_range_chunks_init();

// --- The running save ---
int saved_map();
void set_saved_map(int map);
void set_save_paper_map(int page);
void save_mark_paper_map(int page);
void save_mark_paper_maps(std::uint32_t pages);
struct SavedPlayer {
    std::int32_t location = 0;
    std::int32_t x = 0, z = 0;
    std::int16_t rotation_y = 0;
    std::int32_t health = 0;
};
SavedPlayer saved_player();
void store_saved_player(const SavedPlayer& p);
std::int32_t autosave_health();
void autosave_count_continue(int max);
bool saved_enemy_alive(int row);   // this map's enemy of the spawn row is not killed for good
void forget_saved_enemy(int row);  // ... is killed for good
std::uint32_t item_toggle_flags();
void poll_saves();
int save_catalog_count();  // saves CONTINUE can load
int save_listed_count();   // saves the load list shows (those and the saves of a newer game, which it refuses)

// --- The player's and the NPCs' character records ---
void* player_record();
int player_used_item();
void set_player_used_item(int item);
void set_player_rotation(std::int16_t x, std::int16_t y, std::int16_t z);
bool player_visible();
void draw_player();
bool hostile_npc_alive();
void* npc_record(int slot);
int npc_slot_of(const void* record);  // index into the NPC slots (negative for records before them)
int npc_chara(int slot);
int npc_group(int slot);
void npc_fill(int slot, const NpcSlotFill& fill);
void npc_spawn_at(int slot, int chara, int row, int state_step, std::int32_t x, std::int32_t y, std::int32_t z,
                  std::int16_t yaw);
struct NpcRecordView {
    std::int32_t chara = 0;
    std::int32_t x = 0, z = 0;
    std::int32_t health = 0;
    bool no_radio_static = false;
};
NpcRecordView npc_record_view(int slot);
void npc_free(int slot);
bool npc_visible(int slot);
void npc_set_visible(int slot, bool on);
void npc_unlock_animation(int slot);
bool npc_has_behaviour(int slot);
void npc_draw(int slot, int anim);
std::int32_t npc_control_state(int slot);
void npc_restart_state(int slot);
void npc_behaviour_run(int slot, int anim);
bool npc_debug_spawned(int slot);
bool anim_data_ready(int anim);
int chara_anim_data_index(int chara);  // the character's animation data slot as the table stores it
int chara_record_id(const void* chara);
void set_chara_record_id(void* chara, int id);
int chara_record_group(const void* chara);
void chara_record_xz(const void* chara, std::int32_t& x, std::int32_t& z);
std::int32_t chara_record_health(const void* chara);
std::int32_t chara_record_damage(const void* chara);
std::uint32_t chara_record_flags(const void* chara);
void set_chara_record_flags(void* chara, std::uint32_t flags);
std::uint32_t chara_record_heading_word(const void* chara);
int chara_record_attack_received(const void* chara);
void attack_tick_reset(void* chara);
void attack_settle(void* chara);
void chara_load(std::int32_t model, std::int32_t chara, void* bones, std::int8_t force_free, void* lm, void* tex);
void chara_process_loads();
void chara_bones_init(std::int32_t anim);

// --- World, collision, camera ---
std::int32_t collision_ground(std::int32_t x, std::int32_t z);
std::uint16_t collision_flags();
void set_collision_flags(std::uint16_t flags);
std::uint16_t collision_trigger_flags();
void set_collision_trigger_flags(std::uint16_t flags);
bool spawn_position_ok(std::int32_t x, std::int32_t z);
void view_position_xz(std::int32_t& x, std::int32_t& z);
bool long_view_active();
bool world_point_in_view(std::int32_t x, std::int32_t z);
std::int32_t stereo_balance(std::int32_t x, std::int32_t z);
std::int32_t square_root_q12(std::int32_t v);
bool fps_camera();
void reseat_third_person_camera(std::int32_t yaw);
void camera_move_and_set();
void debug_camera_update();
void camera_move(const Waypoint& target, const CameraParams& params, bool warp);
void camera_watch(const Waypoint& target, const CameraParams& params, bool warp);
void flashlight_toggle();
void flashlight_update();
void effects_2d_update();
void sustain_positional_loops();
void draw_world();
void frame_world_widescreen(bool cutscene_live);
void crosshair_draw();

// --- Frame, timing, random ---
void set_delta_time(std::int32_t delta);
std::int32_t vblanks();
void set_interval_vblanks(int n);
void wait_draw();
void demo_random_backup();
void demo_random_restore();
void demo_random_advance();
void game_timer_update();
void set_transition_start_ms(int ms);
void warm_boot();
void flush_debug_log();

// --- Screen, fades, pictures, VRAM, 2D ---
bool screen_fade_none();
bool screen_fade_white();
void screen_fade_reset();
void cutscene_borders_in();
void cutscene_borders_out();
void screen_init(bool interlaced);
void screen_refresh();
void clear_interlaced_screen();
void keep_black_clear();
std::int32_t background_gamma();
void set_background_gamma(std::int32_t gamma);
void background_motion_blur_now();
void background_motion_blur_wait();
void hold_last_frame();
void release_frozen_frame();
void cancel_frozen_frame_release();
void arm_feedback_store();
void set_map_screen_active(bool on);
enum class Picture : std::uint8_t {
    ItemInspection, EventSecondary, PaperMap, PaperMapMarkings, DeathTip, Title
};
ImagePlace picture_place(Picture p);
void read_image(std::int32_t file, Picture p);
void draw_picture(Picture p);
void draw_picture_alt(Picture p);
enum class OtList : std::uint8_t { Main, Overlay };
enum class Packet : std::uint8_t { GouraudQuad, Mode, Sprite, Page };
void draw_paused_text();

// --- Files ---
bool file_chunks_loaded();
void seek_file(std::int32_t file);
void wait_file_queue();
void reset_file_queue();
int file_queue_length();
void load_options_screen();
void load_movie_stream();
void play_event_movie(int param);
void load_map_item_textures(int map);
void load_unique_item_model(int item);
void inventory_map_set(int map);
void boot_map_load(int map);
void boot_player_init();
void boot_game_startup();
void boot_savegame_initialize(int map, std::int32_t difficulty);
void new_game_hooks();  // the game script, then the mods' hook with the difficulty
void console_apply_pending_flags();

// --- Messages, items, pickups ---
int map_message_draw(int message);
void set_message_game_voiced(bool on);
// The voice of the text box stops: the box is taken over by another message.
void stop_message_voice();
void start_item_pickup(int item);
bool animate_pickup_item(int item);
void set_pickup_item_active(bool on);
int ammo_pickup_count(int item);
void play_sound_at(int sound, const Waypoint* at);

// --- Sound ---
void stop_sound(int sound);
void sound_attributes(int sound, std::int8_t balance, std::uint8_t volume);
bool audio_streaming();
void music_track_update();
void music_track_change(int map);
void gas_sound_contact(int volume);
void gas_sound_motor(bool chainsaw, int volume);
void gas_sound_drill_run(int volume);

// --- Pad ---
bool pad_exchange_done();
int pad_state(int port);
int pad_info(int port, int term);
int pad_set_main_mode(int port, int mode, int lock);
void pad_set_actuators(int port, std::uint8_t* bytes, int count);

// --- PC features ---
bool pause_menu_native();
void pause_menu_enter();
void pause_menu_reset();
void pause_menu_update();
bool pause_menu_stops_clock();
bool pause_menu_return_pending();
bool hook_map_event_allowed(int param);
bool hook_item_pickup(int item, int count, int flag);
int hook_item_pickup_count(int item, int flag, int count);
void hook_item_picked(int item, int count, int flag);
// item.given (a story event adds an item without a pickup): the count to add, -1 cancelled.
int hook_item_given(int item, int count, const char* source);
bool hook_radio_static(int heard, int nearest_slot, std::uint32_t nearest_sq);
bool script_map_handler(int param);
bool runtime_pickups_run_event();
void runtime_pickups_draw();
bool runtime_pickups_start(std::string_view id);
bool runtime_pickups_try_start(std::int32_t x, std::int32_t y, std::int32_t z,
                               const std::function<bool(std::int32_t, std::int32_t)>& facing);
bool pick_npc_tick(int slot);
bool keyframe_viewer_on(int slot);
void keyframe_viewer_pose(int anim);
int difficulty_count();
const char* difficulty_label(int index);
int difficulty_default();
std::int32_t difficulty_base(int index);
const char* difficulty_start_map(int index);

// --- Over the running map and the engine's records (game_link_records.cpp) ---
// The map overlay: its event list (12-byte records, decoded by map_event) and map points, null without them; its
// event scripts and the character path and animation hooks of the NPCs.
void* map_event_table();
const MapPointRecord* map_point_table();
bool overlay_event_script_present(int index);
void overlay_event_script(int index);
bool overlay_chara_path(void* chara, std::int32_t anim, std::int32_t* waypoints, std::int16_t heading,
                        std::int32_t count);
std::int32_t overlay_npc_anim(AnimOp op, void* chara, std::int32_t state);
int world_map_type();
std::string_view world_map_tag();  // empty without a map
bool map_chunked_interior();
void nearby_collision_triggers(std::int32_t x, std::int32_t z);
// The animation data slot's clip header (null when none is loaded) and its bone transforms.
void* anim_header(int anim);
void* anim_bones(int anim);
void npc_pose_still(int slot, int anim, bool idle, std::int32_t first, std::int32_t last, std::int32_t duration);
// A character nothing animates any more: holds the keyframe its record names and places the root.
void npc_pose_frozen(int slot, int anim);
std::int32_t attack_kill_class(std::int32_t attack);
// The picture's image record (where it sits in texture memory).
void* picture_record(Picture p);
// Texture memory and the order tables.
void clear_title_area();
void swap_display_buffers();
void draw_paper_map_sheet();
void keep_event_background_area();
void restore_event_background_area();
void keep_paper_map_area(bool pickup);
void restore_paper_map_area(bool pickup);
void keep_movie_area();
void restore_movie_area();
void submit_2d(OtList list, int entry, const sh::draw2d::Command& command, Packet packet);

}  // namespace sh::gameflow::game
