// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The narrow link of the script binds in the engine (src/engine/scripting/*.cpp, src/engine/script/named_api*.cpp)
// to the rest of the game: functions of other modules by their readable C names (plain numbers and opaque
// addresses only), and accessors over the running game's records, implemented in the engine by
// script_game_link.cpp.

#pragma once

#include <cstddef>
#include <cstdint>

namespace sh::scripting::link {

// --- Accessors over the engine's records (script_game_link.cpp) ---------------------------------------------------

// The world object draw record: its size, where its position (3 x s32) and rotation (3 x s16) lie, and the size of
// the placement record (a model and a position) some objects are kept in.
struct ObjectRecordLayout {
    std::size_t pose_bytes = 0;
    std::size_t position = 0;
    std::size_t rotation = 0;
    std::size_t placement_bytes = 0;
};
ObjectRecordLayout object_record_layout();
// Hands the draw record at `record` (a world object of the loaded map) to this frame's world draw list.
void object_record_draw(void* record, const std::int32_t position[3], const std::int16_t rotation[3]);

// The game's frame values the scripts read and write by name.
enum class FrameValue : std::uint8_t {
    DeltaTime, DeltaTimeRaw, GravitySpeed, VBlanks, ActiveBuffer, ScreenFadeStatus, BackgroundGamma, IntervalVBlanks,
    ScreenFadeTimestep,
};
long long frame_value(FrameValue v);
void set_frame_value(FrameValue v, long long value);

// The pictures the event scripts draw from (the file queue's 8-byte image records).
enum class Picture : std::uint8_t { ItemInspection, EventSecondary, PaperMap, LoadingScreen };
void* picture_record(Picture p);

// The animation buffer size of character group `group` (0..3); false for a bad group.
bool set_anim_buffer_size(int group, std::int32_t bytes);

// The map's character hooks: the first keyframe of the active clip, whether NPCs can walk event waypoints, the clip
// reset (to the still pose or not; false when the map has none).
int chara_anim_start_keyframe(void* record);
bool chara_path_walker_present();
bool chara_anim_reset(void* record, bool to_still);
// The bone transforms of the animation slot of character type `chara` (nullptr when it has none).
const void* chara_anim_bones(int chara);

// The item close-up view of the puzzle scenes: the inspected models of the TMD file, their transforms and the
// camera (ItemViewField order: rotation, scale, translation, position).
void item_view_read(int tim, int tmd);
void item_view_camera_init();
void item_view_models_map();
void item_view_model_link(int slot, int object);
bool item_view_model_linked(int slot);
void item_view_get(int slot, int field, std::int32_t out[3]);
void item_view_set(int slot, int field, const std::int32_t v[3], int mask);
bool item_view_model_rotate(int slot);
void item_view_begin();
void item_view_finish();
void item_view_model_draw(int slot, int mode);
void item_view_precise_depth();

// The GPU lights of the item models, and a 2D gouraud polygon (3 or 4 corners of x, y, 0xRRGGBB) at an entry of
// the overlay ordering table.
void flat_light(int index, const std::int32_t direction[3], int r, int g, int b);
void ambient_light(int r, int g, int b);
void light_mode(int mode);
void ui_gouraud(int depth, bool semi, int corners, const int (*xy_rgb)[3]);

}  // namespace sh::scripting::link
