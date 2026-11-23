// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Script binds of the map events: the flat sh.<name> functions the map0-map3 handlers call and
// the named
// sh.<group>.<name> functions of the cutscene, close-up, effect and character scenes.
//
// The engine owns the names, the argument checks, the answers and everything it can do on its own records: the
// light rig, the system flags and the running save (sh::game::session()), controller 1, the planar length, the
// paper map and cursor drawing (sh::screens), the debug text position (sh::text), the camera's default speeds
// (sh::camera) and the item view's display list (sh::inventory). Whatever still lives in the running game (the
// file queue, the character records, the overlay's functions, the item screen's models, the effect pools) is asked
// of a MapEventHost, one call per game action.
//
// Units are the script API's: positions Q19.12, angles Q3.12, time Q12 seconds. A character is its slot
// (0 the player, 1.. the NPC slots), a file buffer its number, map state a registered name (sh.map.get names).

#pragma once

#include "engine/script/sh_script.h"

#include <cstdint>
#include <optional>

namespace sh::scripting {

struct Point {
    std::int32_t x = 0, y = 0, z = 0;
};

// The character record fields sh.chara_field_get / chara_field_set name, in the order of their field codes.
enum class CharaField : int {
    Health,
    MoveSpeed,
    FallSpeed,
    ControlState,
    StateStep,
    AnimTime,
    AnimStatus,
    AnimKeyframe,
    AnimFlags,
    CollisionState,
    Flags,
    CharaId,
    Count
};

// The three transforms of an item view slot and the origin of its frame.
enum class ItemViewField : int { Rotation, Scale, Translation, Position };

class MapEventHost {
public:
    virtual ~MapEventHost() = default;

    // --- Map state ---
    // A map state variable by its registered name (and, with the raw script API on, an overlay symbol).
    virtual void* map_state(const char* name) = 0;
    // Map data of the loaded overlay or the program by symbol (the overlay's own copy first).
    virtual void* map_data(const char* symbol) = 0;
    // A full-screen background image by name: the two engine images or an image map variable; null when none.
    virtual void* image(const char* name) = 0;

    // --- File queue (FS buffer numbers) ---
    // Queue index of the read, or -1 when the buffer number names no buffer.
    virtual int file_read(int file, int buffer) = 0;
    virtual void file_wait() = 0;
    virtual int file_seek(int file) = 0;
    virtual bool file_loaded(int queue_index) = 0;
    virtual int file_queue_length() = 0;
    // The world chunks around the player are set up.
    virtual bool chunks_ready() = 0;
    // The flare's flame picture to its place in video memory.
    virtual void load_flame_texture() = 0;
    // The world chunk of a grid cell becomes IPD file `file`.
    virtual void world_chunk_set(int file, int cell_x, int cell_z) = 0;

    // --- Cutscenes (the cutscene in an FS buffer) ---
    virtual void cutscene_start(int buffer) = 0;
    virtual void cutscene_chara_apply(int slot, const char* track, std::int32_t time, int buffer) = 0;
    // The camera track at `time` becomes the user camera's target and watch point (warped).
    virtual void cutscene_camera_apply(std::int32_t time, int buffer) = 0;

    // --- Camera ---
    virtual void camera_target(const Point& p, bool warp) = 0;
    virtual void camera_watch(const Point& p, bool warp) = 0;
    virtual Point view_position() = 0;
    virtual int stereo_balance(const Point& p) = 0;
    virtual void flashlight_fix() = 0;

    // --- Characters ---
    // A field of the slot's record; nothing when the slot names no record.
    virtual std::optional<std::int32_t> chara_field(int slot, CharaField f) = 0;
    virtual bool set_chara_field(int slot, CharaField f, std::int32_t value) = 0;
    virtual void chara_model_clear(int slot) = 0;
    // A story character's movement back to rest (heading, speed, ground height, run distance).
    virtual void chara_motion_reset(int slot) = 0;
    virtual int chara_anim_start_keyframe(int slot) = 0;
    virtual void chara_anim_unlock_play_to_end(int slot) = 0;
    virtual void chara_bones_init(int index) = 0;
    virtual void chara_path_walk(int slot, int anim, int count, bool advance_step) = 0;
    virtual int chara_anim_playback_state(int slot) = 0;
    // The character's control handed back to its own update; false when the map has no such function.
    virtual bool chara_anim_reset(int slot, bool to_still) = 0;
    // The slot emptied and its kill recorded in the save.
    virtual void chara_remove_saved(int slot) = 0;
    virtual void chara_effect_spawn(int slot, const Point& at, int kind, int variant) = 0;
    virtual void chara_model_buffers_assign(int group_mask) = 0;
    virtual int npc_slot_count() = 0;

    // --- Player ---
    virtual void weapon_stash() = 0;
    virtual void weapon_restore() = 0;
    // false when the map has no such function.
    virtual bool weapon_hide() = 0;
    virtual bool weapon_unequip() = 0;
    virtual void extra_anim_flags_set(std::int32_t flags) = 0;
    virtual bool item_remove(int item, int count) = 0;

    // --- Map effects, messages, paper maps ---
    virtual void map_effects_init(int a, int b) = 0;
    virtual void map_effects_update(int a, int b, int primitive, void* data, int c, int d) = 0;
    virtual void weather_update(int a, int map, int phase) = 0;
    // `map_variant`: the copy of the map scripts (Map_MessageWithAudio) instead of the event one.
    virtual void message_with_audio(bool map_variant, int message, std::uint8_t* index, const std::uint16_t* cmds) = 0;
    virtual void message_dimmed(int file, int fade0, int fade1, int message0, int message1) = 0;
    virtual void paper_map_cmd(int cmd, int paper_map) = 0;
    virtual void math_point_from_angle(const Point& p, std::int32_t angle, int a, int b) = 0;

    // --- Screens ---
    virtual void bg_transition(void* from, void* to, std::int32_t alpha) = 0;
    virtual void credits_roll_start(int ending) = 0;
    virtual bool credits_roll_music_started() = 0;
    virtual bool credits_roll_finish() = 0;

    // --- Item view (close-up models over the item screen) ---
    virtual void item_view_read(int tim, int tmd) = 0;
    virtual void item_view_camera_init() = 0;
    virtual void item_view_models_map() = 0;
    virtual void item_view_model_link(int slot, int object) = 0;
    virtual bool item_view_model_linked(int slot) = 0;
    virtual Point item_view_get(int slot, ItemViewField f) = 0;
    // Components with their mask bit (1 x, 2 y, 4 z) set are written.
    virtual void item_view_set(int slot, ItemViewField f, const Point& v, int mask) = 0;
    // false when the slot has no linked frame.
    virtual bool item_view_model_rotate(int slot) = 0;
    virtual void item_view_begin() = 0;
    virtual void item_view_finish() = 0;
    virtual void item_view_model_draw(int slot, int mode) = 0;
    virtual void item_view_precise_depth() = 0;

    // --- Lights and 2D shapes of a close-up ---
    virtual void flat_light(int index, const Point& direction, int r, int g, int b) = 0;
    virtual void ambient_light(int r, int g, int b) = 0;
    virtual void light_mode(int mode) = 0;
    // x, y: pixels from the screen centre; rgb 0xRRGGBB.
    virtual void ui_gouraud(int depth, bool semi, int corners, const int (*xy_rgb)[3]) = 0;
    virtual void ui_draw_mode(int depth, int blend, int page_x, int page_y) = 0;
    virtual void ui_frame() = 0;

    // --- Effect pools by name ---
    virtual void heat_field_start(const char* name, const Point& at) = 0;
    virtual void heat_field_start_between(const char* name, const Point& from, const Point& to) = 0;
    virtual void heat_field_spawn(const char* name, const Point& around, int count) = 0;
    virtual void heat_field_update(const char* name, bool advance) = 0;
    virtual void fire_pool_attached_reset(const char* name) = 0;
    virtual void fire_pool_attach(const char* name) = 0;
    // Draws nothing when the character of the slot has no posed bones.
    virtual void fire_pool_draw_attached(const char* name, int slot) = 0;
    virtual void bolt_pool_attach(const char* name, const char* owner, std::int32_t x, std::int32_t z) = 0;
    virtual void bolt_pool_update(const char* name) = 0;
    virtual void bgm_play_new(int track) = 0;
};

// The planar length of (x, z) at Q6 precision, back in Q12 (the game's safe 2D magnitude).
std::int32_t planar_length_q6(std::int32_t x, std::int32_t z);

// Serves a named call: the answer count, -1 with `err` set, or kNotMapEventCall for a name of another group.
constexpr int kNotMapEventCall = -2;
int map_event_call(MapEventHost& host, const char* name, int argc, const ShScriptArg* argv, long long* out,
                   int out_cap, char* err, int err_cap);

// Installs the flat functions into `api` and chains its named_call / named_list: a name of this module is served
// here, every other one goes on to the functions the api had.
void bind_map_event_api(ShScriptGameApi& api, MapEventHost& host);

}  // namespace sh::scripting
