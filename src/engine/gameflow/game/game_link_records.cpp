// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game flow link's accessors over the running map and the engine's records (the last section of game_link.h):
// the map's events, points and event handlers, the map cast's hooks, the animation slots, the attack table, the
// pictures' homes in video memory, the areas kept aside around pictures and movies, and the 2D order tables.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "engine/app/game_view.h"
#include "engine/app/loader/game_files.h"
#include "engine/app/map_info_tables.h"
#include "engine/chara/game/map_cast.h"
#include "engine/entities/character_record.h"
#include "engine/entities/game/model_runtime.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/anim_slots.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/inventory/game/attack_table.h"
#include "engine/memory/work_buffers.h"
#include "engine/maps/map_runtime.h"
#include "engine/math/geometry_state.h"
#include "engine/runtime/world_map.h"
#include "engine/screens/screens_view.h"
#include "engine/world/collision_triggers.h"
#include "render/gpu/render_queries.h"
#include "render/gpu/renderer.h"
#include "engine/app/loader/picture_places.h"
#include "render/gpu/render_state.h"

namespace ent = sh::entities;

extern "C" {
// The animation slots (engine/gameflow/game/anim_slots_host.cpp).
// The clip playback and the bone sampler (engine/entities/game/model_runtime.cpp).
void Anim_PlaybackLoop(ent::ModelRecord* model, void* anm_header, ent::BoneCoord* bones, ent::ClipInfoRecord* clip);
void Anim_BoneUpdate(void* anm_header, ent::BoneCoord* bones, std::int32_t keyframe0, std::int32_t keyframe1,
                     std::int32_t alpha);
// The collision triggers near a point.
void Collision_NearbyTriggersGet(std::int32_t posX, std::int32_t posZ, const sh::world::PackedTrigger* triggers);
// The pictures' homes in video memory (engine/app/loader/picture_places.cpp) and the full-screen picture draw.
void Screen_BackgroundImgDraw(sh::loader::ImagePlace* image);
// The frame store the renderer skips for one frame (src/render/gpu/frame_driver.cpp).
}

namespace sh::gameflow::game {

namespace {

// The map type flags: an interior streamed by one or two active chunks.
constexpr std::uint8_t kMapChunkOne = 1u << 0;
constexpr std::uint8_t kMapChunkTwo = 1u << 1;
constexpr std::uint8_t kMapInterior = 1u << 2;

// Where the game keeps the files and the video memory it puts aside: the paper map's sheet file, the area under an
// event background, under the paper map pickup and under a movie.
constexpr sh::memory::WorkBuffer kPaperMapFile = sh::memory::WorkBuffer::PaperMapSheet;
constexpr sh::memory::WorkBuffer kPickupKeep = sh::memory::WorkBuffer::PickupKeep;
constexpr sh::memory::WorkBuffer kBackgroundKeep = sh::memory::WorkBuffer::BackgroundKeep;
constexpr sh::memory::WorkBuffer kMovieKeep = sh::memory::WorkBuffer::MovieKeep;

struct KeepArea {
    int x, y, w, h;
};
// The area a background picture overwrites (3/5 of a screen from the right half of the second buffer), the narrower
// one of the paper map pickup sheet, and the visible frame a movie draws over.
constexpr KeepArea kEventBackgroundArea = {320, 256, 192, 240};
constexpr KeepArea kPickupArea = {320, 256, 160, 240};
constexpr KeepArea kMovieArea = {320, 256, 160, 240};

void keep(const KeepArea& a, sh::memory::WorkBuffer buffer) {
    sh::screens::keep_vram(a.x, a.y, a.w, a.h, sh::memory::work_buffers().as<std::uint16_t>(buffer));
}
void restore(const KeepArea& a, sh::memory::WorkBuffer buffer) {
    sh::screens::restore_vram(a.x, a.y, a.w, a.h, sh::memory::work_buffers().as<const std::uint16_t>(buffer));
}

ent::CharacterRecord* chara(void* p) { return static_cast<ent::CharacterRecord*>(p); }

// A TIM file in memory without a copy: the flags word (bit 3: a palette block comes first), then each block's byte
// length, rectangle (x, y, w, h halfwords) and words.
struct TimBlocks {
    const std::int16_t* palette_rect = nullptr;
    const std::uint16_t* palette = nullptr;
    const std::int16_t* pixel_rect = nullptr;
    const std::uint16_t* pixels = nullptr;
};
bool tim_blocks(const void* data, TimBlocks& out) {
    const auto* w = static_cast<const std::uint32_t*>(data);
    if ((w[0] & 0xFF) != 0x10) {
        return false;
    }
    const std::uint32_t flags = w[1];
    w += 2;
    if (flags & 0x8) {
        out.palette_rect = reinterpret_cast<const std::int16_t*>(&w[1]);
        out.palette = reinterpret_cast<const std::uint16_t*>(&w[3]);
        w += w[0] >> 2;
    }
    out.pixel_rect = reinterpret_cast<const std::int16_t*>(&w[1]);
    out.pixels = reinterpret_cast<const std::uint16_t*>(&w[3]);
    return true;
}

// The sheet's palette lies inside the display area, so the frame store overwrites it every frame and the sheet would
// sample game pixels: each frame of the sheet skips that store once and puts the file back into texture memory.
void paper_map_reupload() {
    sh::render::feedback_control().skip_store = 1;
    TimBlocks tim;
    if (!tim_blocks(sh::memory::work_buffers().data(kPaperMapFile), tim)) {
        return;
    }
    sh::gpu::Rect pixels{tim.pixel_rect[0], tim.pixel_rect[1], tim.pixel_rect[2], tim.pixel_rect[3]};
    const sh::loader::ImagePlace& place = sh::loader::picture_places().paper_map;
    if (place.u != 0xFF) {
        pixels.x = static_cast<std::int16_t>(place.u + ((place.page[1] & 0xF) << 6));
        pixels.y = static_cast<std::int16_t>(place.v + ((place.page[1] << 4) & 0x100));
    }
    sh::gpu::Renderer* gpu = sh::gpu::renderer();
    if (gpu != nullptr) {
        gpu->load_image(pixels, tim.pixels);
    }
    sh::gpu::Rect palette{-1, -1, 0, 0};
    if (tim.palette != nullptr) {
        palette = {tim.palette_rect[0], tim.palette_rect[1], tim.palette_rect[2], tim.palette_rect[3]};
        if (place.palette_x != -1) {
            palette.x = place.palette_x;
            palette.y = place.palette_y;
        }
        if (gpu != nullptr) {
            gpu->load_image(palette, tim.palette);
        }
    }
    GR_DirectUploadVRAMRegion(pixels.x, pixels.y, pixels.w, pixels.h);
    if (palette.w > 0 && palette.h > 0) {
        GR_DirectUploadVRAMRegion(palette.x, palette.y, palette.w, palette.h);
    }
}

}  // namespace

// --- The map ---
void* map_event_table() { return sh::maps::map_runtime().events(); }
const MapPointRecord* map_point_table() { return sh::maps::map_runtime().points(); }
bool overlay_event_script_present(int index) { return sh::maps::map_runtime().event_handler(index); }
void overlay_event_script(int index) { sh::maps::map_runtime().run_event_handler(index); }
bool overlay_chara_path(void* c, std::int32_t anim, std::int32_t* waypoints, std::int16_t heading, std::int32_t count) {
    return sh::chara::game::cast::path_step(chara(c), anim, reinterpret_cast<const ent::Vec3*>(waypoints), heading,
                                            count) != 0;
}
std::int32_t overlay_npc_anim(AnimOp op, void* c, std::int32_t state) {
    namespace cast = sh::chara::game::cast;
    switch (op) {
    case AnimOp::SetState: cast::control_state_set(chara(c), state); return 0;
    case AnimOp::PlaybackState: return cast::anim_playback_state(chara(c));
    case AnimOp::Lock: cast::anim_lock(chara(c)); return 0;
    case AnimOp::Unlock: cast::anim_unlock(chara(c)); return 0;
    case AnimOp::Reset: cast::control_reset(chara(c)); return 0;
    }
    return 0;
}
int world_map_type() { return sh::world::world_map().map_type(); }
std::string_view world_map_tag() { return sh::world::world_map().tag(); }
bool map_chunked_interior() {
    const sh::app::MapTypeRecord* info = sh::app::map_type_record(sh::maps::map_runtime().map_type());
    if (info == nullptr) return false;
    const std::uint8_t flags = info->flags;
    return (flags & kMapInterior) && (flags & (kMapChunkOne | kMapChunkTwo)) != 0;
}
void nearby_collision_triggers(std::int32_t x, std::int32_t z) {
    Collision_NearbyTriggersGet(x, z, static_cast<const sh::world::PackedTrigger*>(sh::world::world_map().collision_triggers()));
}

// --- Animation data and the attack table ---
void* anim_header(int anim) { return sh::gameflow::anim_slot_table().slots[anim].active_header; }
void* anim_bones(int anim) { return sh::gameflow::anim_slot_table().slots[anim].bone_coords; }
void npc_pose_still(int slot, int anim, bool idle, std::int32_t first, std::int32_t last, std::int32_t duration) {
    ent::CharacterRecord& c = sh::game::session().system.characters.npcs[slot];
    void* header = sh::gameflow::anim_slot_table().slots[anim].active_header;
    auto* bones = static_cast<ent::BoneCoord*>(sh::gameflow::anim_slot_table().slots[anim].bone_coords);
    if (header == nullptr) {
        return;
    }
    if (idle) {
        ent::ClipInfoRecord clip{};
        clip.playback = Anim_PlaybackLoop;
        clip.duration.constant = duration;
        clip.start_keyframe = static_cast<std::int16_t>(first);
        clip.end_keyframe = static_cast<std::int16_t>(last);
        Anim_PlaybackLoop(&c.model, header, bones, &clip);
    } else {
        Anim_BoneUpdate(header, bones, 0, 0, 0);
    }
    sh::math::transform_of(bones->local).rot = sh::math::rot_matrix_zxy_neg({c.rotation.x, c.rotation.y, c.rotation.z});
    // The root bone's translation is in geometry units (Q8).
    bones->local.t[0] = c.position.x >> 4;
    bones->local.t[1] = c.position.y >> 4;
    bones->local.t[2] = c.position.z >> 4;
    bones->world_current = 0;
}
void npc_pose_frozen(int slot, int anim) {
    ent::CharacterRecord& c = sh::game::session().system.characters.npcs[slot];
    void* header = sh::gameflow::anim_slot_table().slots[anim].active_header;
    auto* bones = static_cast<ent::BoneCoord*>(sh::gameflow::anim_slot_table().slots[anim].bone_coords);
    if (header == nullptr) {
        return;
    }
    Anim_BoneUpdate(header, bones, c.model.anim.keyframe, c.model.anim.keyframe, 0);
    sh::math::transform_of(bones->local).rot = sh::math::rot_matrix_zxy_neg({c.rotation.x, c.rotation.y, c.rotation.z});
    bones->local.t[0] = c.position.x >> 4;
    bones->local.t[1] = c.position.y >> 4;
    bones->local.t[2] = c.position.z >> 4;
    bones->world_current = 0;
}
std::int32_t attack_kill_class(std::int32_t attack) {
    const std::vector<sh::inventory::game::AttackRow>& table = sh::inventory::game::attack_table();
    const auto at = static_cast<std::size_t>(attack);
    return attack >= 0 && at < table.size() ? table[at].hit_type : 0;
}

// --- Pictures, texture memory, order tables ---
void* picture_record(Picture p) {
    switch (p) {
    case Picture::ItemInspection: return &sh::loader::picture_places().item_inspection;
    case Picture::EventSecondary: return &map_event_state().secondary_background;
    case Picture::PaperMap: return &sh::loader::picture_places().paper_map;
    case Picture::PaperMapMarkings: return &sh::loader::picture_places().paper_map_markings;
    case Picture::DeathTip: return &sh::loader::picture_places().death_tip;
    case Picture::Title: return &sh::loader::picture_places().title;
    }
    return &sh::loader::picture_places().item_inspection;
}
void clear_title_area() {
    if (sh::gpu::Renderer* gpu = sh::gpu::renderer()) {
        gpu->clear(sh::gpu::Rect{0, 32, 320, 448}, 0, 0, 0);
    }
}
void swap_display_buffers() { sh::app::view::swap_buffers(); }
void draw_paper_map_sheet() {
    paper_map_reupload();
    Screen_BackgroundImgDraw(&sh::loader::picture_places().paper_map);
}
void keep_event_background_area() { keep(kEventBackgroundArea, kBackgroundKeep); }
void restore_event_background_area() { restore(kEventBackgroundArea, kBackgroundKeep); }
void keep_paper_map_area(bool pickup) {
    if (pickup) {
        keep(kPickupArea, kPickupKeep);
    } else {
        keep(kEventBackgroundArea, kBackgroundKeep);
    }
}
void restore_paper_map_area(bool pickup) {
    if (pickup) {
        restore(kPickupArea, kPickupKeep);
    } else {
        restore(kEventBackgroundArea, kBackgroundKeep);
    }
}
void keep_movie_area() { keep(kMovieArea, kMovieKeep); }
void restore_movie_area() {
    sh::app::view::swap_buffers();
    restore(kMovieArea, kMovieKeep);
}
void submit_2d(OtList list, int entry, const sh::draw2d::Command& command, Packet packet) {
    // The packet the game's drawer built: a native tag pointer and the retail packet's words.
    constexpr std::size_t kTag = sizeof(void*);
    std::size_t bytes = 0;
    switch (packet) {
    case Packet::GouraudQuad: bytes = kTag + 36; break;
    case Packet::Mode: bytes = kTag + 12; break;
    case Packet::Sprite: bytes = kTag + 20; break;
    case Packet::Page: bytes = kTag + 8; break;
    }
    const sh::screens::Table2d table = list == OtList::Main ? sh::screens::Table2d::Hud : sh::screens::Table2d::NextBack;
    auto* cursor = static_cast<std::uint8_t*>(sh::screens::packet_cursor());
    sh::screens::link_2d(table, entry, cursor, command);
    sh::screens::set_packet_cursor(cursor + bytes);
}

}  // namespace sh::gameflow::game
