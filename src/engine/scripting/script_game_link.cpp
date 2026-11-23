// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The script binds' accessors over the running game's engine records (script_game_link.h and the character, clip
// and view parts of binds/record_links.h): the world object records, the frame clock, the pictures' homes, the map
// cast's hooks, the animation slots, the item close-up stage and Harry's map clips.

#include "engine/scripting/script_game_link.h"
#include "engine/loader/active_map.h"
#include "engine/scripting/binds/record_links.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "engine/app/loader/game_files.h"
#include "engine/chara/game/chara_file_table.h"
#include "engine/chara/game/chara_records.h"
#include "engine/chara/game/clip_tables.h"
#include "engine/chara/game/map_cast.h"
#include "engine/camera/camera_types.h"
#include "engine/effects/drawer_projection.h"
#include "engine/entities/game/model_runtime.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/anim_slots.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/inventory/game/item_stage.h"
#include "engine/memory/work_buffers.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/maps/map_runtime.h"
#include "engine/maps/map_tables.h"
#include "engine/math/geometry_state.h"
#include "engine/runtime/world_object_record.h"
#include "engine/screens/screens_view.h"
#include "engine/tables/anim_table_registry.h"
#include "render/gpu/render_state.h"
#include "engine/loop/frame_view.h"
#include "engine/app/loader/picture_places.h"
#include "engine/game_state/game_state_ui.h"

namespace ent = sh::entities;
namespace cg = sh::chara::game;

extern "C" {
// The frame clock (engine/app/game_view.cpp), the buffer being drawn, the fade (ui/screens/screens_host.cpp).
// The pictures' homes in video memory (engine/app/loader/picture_places.cpp).
// The animation slots (engine/gameflow/game/anim_slots_host.cpp).
// The puzzle close-up draws with exact depths this frame (engine/inventory/game/item_screens_draw.cpp).
extern int g_PcPuzzleItemDepth;
// The world drawer (render/world/world_objects_draw.cpp), the active clip and its speed.
void WorldGfx_ObjectAdd(void* model, const void* position, const void* rotation);
ent::ClipInfoRecord* ModelAnim_ActiveInfoGet(ent::ModelAnimRecord* anim);
std::int32_t Anim_DurationGet(ent::ModelRecord* model, ent::ClipInfoRecord* clip);
void Vw_WorldScreenMatrixAtPositionGet(sh::camera::Matrix* matrix, std::int32_t x, std::int32_t y, std::int32_t z);
}

namespace {

constexpr int kGroupSlots = 4;

}  // namespace

namespace sh::scripting::link {

ObjectRecordLayout object_record_layout() {
    using sh::world::WorldObjectPoseRecord;
    return {sizeof(WorldObjectPoseRecord), offsetof(WorldObjectPoseRecord, position),
            offsetof(WorldObjectPoseRecord, rotation), sizeof(sh::world::WorldObjectPlacementRecord)};
}

void object_record_draw(void* record, const std::int32_t position[3], const std::int16_t rotation[3]) {
    const sh::world::ObjectPosition pos = {position[0], position[1], position[2]};
    const sh::world::ObjectRotation rot = {rotation[0], rotation[1], rotation[2]};
    WorldGfx_ObjectAdd(&static_cast<sh::world::WorldObjectPoseRecord*>(record)->object, &pos, &rot);
}

long long frame_value(FrameValue v) {
    switch (v) {
        case FrameValue::DeltaTime: return sh::loop::frame_delta();
        case FrameValue::DeltaTimeRaw: return sh::loop::frame_delta_raw();
        case FrameValue::GravitySpeed: return sh::loop::frame_gravity();
        case FrameValue::VBlanks: return sh::loop::frame_vblanks();
        case FrameValue::ActiveBuffer: return sh::game_state::frame_buffer_index();
        case FrameValue::ScreenFadeStatus: return sh::screens::fade_control().status;
        case FrameValue::BackgroundGamma: return sh::screens::fade_control().background_gamma;
        case FrameValue::IntervalVBlanks: return sh::loop::frame_interval_vblanks();
        case FrameValue::ScreenFadeTimestep: return sh::screens::fade_control().timestep;
    }
    return 0;
}

void set_frame_value(FrameValue v, long long value) {
    const auto word = static_cast<std::int32_t>(value);
    switch (v) {
        case FrameValue::DeltaTime: sh::loop::frame_delta() = word; break;
        case FrameValue::DeltaTimeRaw: sh::loop::frame_delta_raw() = word; break;
        case FrameValue::GravitySpeed: sh::loop::frame_gravity() = word; break;
        case FrameValue::VBlanks: sh::loop::frame_vblanks() = word; break;
        case FrameValue::ActiveBuffer: sh::game_state::set_frame_buffer_index(word); break;
        case FrameValue::ScreenFadeStatus: sh::screens::fade_control().status = word; break;
        case FrameValue::BackgroundGamma: sh::screens::fade_control().background_gamma = static_cast<std::uint8_t>(value); break;
        case FrameValue::IntervalVBlanks: sh::loop::frame_interval_vblanks() = word; break;
        case FrameValue::ScreenFadeTimestep: sh::screens::fade_control().timestep = word; break;
    }
}

void* picture_record(Picture p) {
    switch (p) {
        case Picture::ItemInspection: return &sh::loader::picture_places().item_inspection;
        case Picture::EventSecondary: return &sh::gameflow::map_event_state().secondary_background;
        case Picture::PaperMap: return &sh::loader::picture_places().paper_map;
        case Picture::LoadingScreen: return &sh::loader::picture_places().loading_screen;
    }
    return nullptr;
}

int chara_anim_start_keyframe(void* record) {
    return sh::maps::map_runtime().has_cast() ? cg::cast::anim_start_keyframe(static_cast<ent::CharacterRecord*>(record))
                                              : 0;
}

bool chara_anim_reset(void* record, bool to_still) {
    if (!sh::maps::map_runtime().has_cast()) {
        return false;
    }
    cg::cast::control_end(static_cast<ent::CharacterRecord*>(record), to_still ? 1u : 0u);
    return true;
}

const void* chara_anim_bones(int id) {
    if (id <= 0) {
        return nullptr;
    }
    const int idx = sh::gameflow::anim_slot_table().chara_slot_of(id);
    if (!sh::gameflow::anim_slot_table().has_slot(idx)) {
        return nullptr;
    }
    return sh::gameflow::anim_slot_table().slots[idx].bone_coords;
}

namespace {
// The close-up camera of the lock and the model file its reels come from.
sh::inventory::ItemCamera s_itemCamera{};
std::string s_itemModel;

std::int32_t* item_wide(int slot, int field) {
    sh::inventory::StagePlace& p = sh::inventory::item_stage().place(slot);
    switch (field) {
        case 1: return p.transform.scale.data();
        case 2: return p.transform.translate.data();
        default: return p.frame.local.t;
    }
}
}  // namespace

void item_view_read(int tim, int tmd) {
    sh::loader::queue_read_image(tim, sh::memory::work_buffers().data(sh::memory::WorkBuffer::FileStaging),
                         &sh::loader::picture_places().event_model_textures);
    sh::loader::queue_read(tmd, sh::memory::work_buffers().data(sh::memory::WorkBuffer::EventModel));
    // The stage draws the model from the import; the read keeps the queue's timing.
    s_itemModel = sh::loader::disc_files().path(tmd);
}
void item_view_camera_init() { sh::inventory::item_stage().camera_init(s_itemCamera); }
void item_view_models_map() {}
void item_view_model_link(int slot, int object) { sh::inventory::item_stage().link(slot, s_itemModel, object, 0); }
bool item_view_model_linked(int slot) { return sh::inventory::item_stage().place(slot).framed; }
void item_view_get(int slot, int field, std::int32_t out[3]) {
    if (field == 0) {
        const auto& r = sh::inventory::item_stage().place(slot).transform.rotate;
        out[0] = r[0];
        out[1] = r[1];
        out[2] = r[2];
        return;
    }
    const std::int32_t* w = item_wide(slot, field);
    out[0] = w[0];
    out[1] = w[1];
    out[2] = w[2];
}
void item_view_set(int slot, int field, const std::int32_t v[3], int mask) {
    if (field == 0) {
        auto& r = sh::inventory::item_stage().place(slot).transform.rotate;
        for (int k = 0; k < 3; ++k) {
            if (mask & (1 << k)) r[static_cast<std::size_t>(k)] = static_cast<std::int16_t>(v[k]);
        }
        return;
    }
    std::int32_t* w = item_wide(slot, field);
    for (int k = 0; k < 3; ++k) {
        if (mask & (1 << k)) w[k] = v[k];
    }
}
bool item_view_model_rotate(int slot) {
    if (!sh::inventory::item_stage().place(slot).framed) {
        return false;
    }
    sh::inventory::item_stage().rotate(slot);
    return true;
}
void item_view_begin() { sh::inventory::item_stage().view_set(s_itemCamera); }
void item_view_finish() {}
void item_view_model_draw(int slot, int mode) {
    sh::inventory::item_stage().draw(slot, static_cast<sh::inventory::StageDraw>(mode));
}
void item_view_precise_depth() { g_PcPuzzleItemDepth = 1; }

// The item close-ups are unlit (their polygons carry no light flag): the flat lights and the light mode have no
// reader; the ambient colour is the geometry state's background colour (Q12 in, the state keeps Q8).
void flat_light(int, const std::int32_t*, int, int, int) {}
void ambient_light(int r, int g, int b) { sh::math::geometry().set_background_color(r >> 4, g >> 4, b >> 4); }
void light_mode(int) {}
void ui_gouraud(int depth, bool semi, int corners, const int (*p)[3]) {
    sh::draw2d::Polygon poly;
    poly.count = static_cast<std::uint8_t>(corners);
    poly.gouraud = true;
    poly.shade.translucent = semi;
    for (int i = 0; i < corners; ++i) {
        poly.xy[i] = {static_cast<std::int16_t>(p[i][0]), static_cast<std::int16_t>(p[i][1])};
        poly.color[i] = {static_cast<std::uint8_t>(p[i][2] >> 16), static_cast<std::uint8_t>(p[i][2] >> 8),
                         static_cast<std::uint8_t>(p[i][2])};
    }
    sh::screens::submit_2d_packet(sh::screens::Table2d::Hud, depth, poly,
                                  corners == 3 ? sh::screens::Packet2d::PolyG3 : sh::screens::Packet2d::PolyG4);
}

bool set_anim_buffer_size(int group, std::int32_t bytes) {
    if (group < 0 || group >= kGroupSlots) {
        return false;
    }
    sh::gameflow::anim_slot_table().slots[group].alloc_size = bytes;
    return true;
}

}  // namespace sh::scripting::link

namespace sh::script::links {

namespace {

ent::CharacterRecord* record(int slot) { return sh::game::session().system.characters.record(slot); }

// Byte offsets in the variable block: the carousel words of the story characters' block and Split Head's flags.
constexpr std::size_t kRideState = 0x04;    // s16
constexpr std::size_t kRideHorse = 0x16;    // s16
constexpr std::size_t kRideHeight = 0x18;   // Q12
constexpr std::size_t kSplitHeadFlags = 0;  // u16

}  // namespace

bool chara_active_clip(int slot, std::int32_t* duration, std::int32_t* start_keyframe) {
    ent::CharacterRecord* ch = record(slot);
    if (ch->model.anim.clip_table == nullptr) {
        return false;
    }
    ent::ClipInfoRecord* info = ModelAnim_ActiveInfoGet(&ch->model.anim);
    *duration = Anim_DurationGet(&ch->model, info);
    *start_keyframe = info->start_keyframe;
    return true;
}

void chara_path_walk_no_wait(int slot, int anim, int mode) {
    // Only a map with a cast walks its NPCs along waypoints.
    if (sh::maps::map_runtime().has_cast()) {
        const sh::gameflow::MapEventState& e = sh::gameflow::map_event_state();
        cg::cast::path_step(record(slot), anim, reinterpret_cast<const ent::Vec3*>(e.waypoints[1]),
                            e.waypoint_headings[1], mode);
    }
}

std::int32_t chara_ride_state(int slot) { return cg::var_read<std::int16_t>(*record(slot), kRideState); }
std::int32_t chara_ride_horse(int slot) { return cg::var_read<std::int16_t>(*record(slot), kRideHorse); }
void chara_set_ride_height(int slot, std::int32_t height) { cg::var_write<std::int32_t>(*record(slot), kRideHeight, height); }
std::int32_t chara_split_head_flags(int slot) { return cg::var_read<std::uint16_t>(*record(slot), kSplitHeadFlags); }

int chara_group_anim_chara(int group) { return sh::gameflow::anim_slot_table().slots[group + 1].active_chara; }

void chara_anim_file_set(int type, int file) { cg::chara_file_row(type).anim = static_cast<std::int16_t>(file); }

int player_map_anim_infos_set(int first, const char* table, int count) {
    const cg::ClipTableRef clips = cg::clip_table(table);
    if (clips.clips == nullptr) {
        return 1;
    }
    auto* dest = static_cast<cg::ClipInfo*>(sh::maps::map_runtime().player_anim_infos());
    if (first < 0 || count < 0 || count > clips.count || dest == nullptr) {
        return 2;
    }
    for (int i = 0; i < count; ++i) {
        dest[first + i] = clips.clips[i];
    }
    return 0;
}

int player_map_anim_ranges_set(int first, const char* symbol, int count) {
    std::uint32_t bytes = 0;
    const auto* rows = static_cast<const sh::maps::PlayerAnimRow*>(MapTables_Array(sh::loader::active_map_name(), symbol, &bytes));
    if (rows == nullptr) {
        return 1;
    }
    sh::maps::PlayerAnimRow* dest = sh::maps::map_runtime().player_anim_rows();
    if (first < 0 || count < 0 || static_cast<unsigned long long>(count) * sizeof(sh::maps::PlayerAnimRow) > bytes ||
        dest == nullptr) {
        return 2;
    }
    for (int i = 0; i < count; ++i) {
        dest[first + i] = rows[i];
    }
    return 0;
}

sh::math::ViewProjection view_at(std::int32_t x, std::int32_t y, std::int32_t z) {
    sh::camera::Matrix view;
    Vw_WorldScreenMatrixAtPositionGet(&view, x, y, z);
    sh::math::ViewProjection vp = sh::math::game_view_projection();
    sh::effects::drawer::set_matrix(vp, &view);
    return vp;
}

bool precise_view_space() { return sh::render::view_space_wanted(); }

}  // namespace sh::script::links
