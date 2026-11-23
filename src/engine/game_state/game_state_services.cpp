// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game-state API (game_state*.h) over the engine's services: the fixed-point math, the camera and the view,
// the screen fade, the frame counters, the sound driver, the disc file table and its queue, the inventory menu's
// selection and the player model switches. The calls into other modules by C name are listed in
// game_state_old_calls.h.

#include "engine/loader/active_map.h"
#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_app.h"
#include "engine/game_state/game_state_map_overlay.h"
#include "engine/game_state/game_state_old_calls.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/game_state/session.h"

#include "audio/game/game_sound_driver.h"
#include "engine/app/gameflow/game_flow.h"
#include "engine/app/loader/game_files.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/game_camera.h"
#include "engine/camera/view_space.h"
#include "engine/camera/view_system.h"
#include "engine/gconfig/sound_tables.h"
#include "engine/inventory/item_view.h"
#include "engine/memory/work_buffers.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/loop/frame_view.h"
#include "engine/math/geometry_state.h"
#include "engine/screens/screens_view.h"
#include "engine/world/fixed_trig.h"
#include "render/gpu/render_state.h"
#include "engine/app/loader/picture_places.h"

#include <memory>

// The camera's stereo placement of a point (src/engine/camera/camera_entry.cpp).

namespace sh::game_state {

namespace {

// The double buffer the frame draws into and where the running map's overlay bytes go (attach_work_buffers).
std::int32_t s_frame_buffer_index = 0;
void* s_map_overlay_buffer = nullptr;

sh::loader::DiscFiles& files() { return sh::loader::disc_files(); }

DiscExtent extent_of(int file) {
    DiscExtent e;
    e.start_sector = files().first_sector(file);
    e.size_bytes = files().bytes(file);
    return e;
}

const char* disc_file_path(DiscFile file) {
    switch (file) {
        case DiscFile::Font16Tim: return "1ST/FONT16.TIM";
        case DiscFile::OptionOverlay: return "VIN/OPTION.BIN";
        case DiscFile::SaveLoadOverlay: return "VIN/SAVELOAD.BIN";
        case DiscFile::TitleTim: return "TIM/TITLE.TIM";
        case DiscFile::TitleETim: return "TIM/TITLE_E.TIM";
        case DiscFile::WarningTim: return "1ST/2ZANKO_E.TIM";
    }
    return "1ST/FONT16.TIM";
}

sh::loader::ImagePlace* image_place(ImageSlot slot) {
    if (slot == ImageSlot::Title) {
        return std::addressof(sh::loader::picture_places().title);
    }
    return std::addressof(sh::loader::picture_places().font16_atlas);
}

void* work_buffer(sh::memory::WorkBuffer buffer) { return sh::memory::work_buffers().data(buffer); }

// Screen fade states (low 3 bits of the status word) and the white bit.
constexpr std::int32_t kFadeReset = 0;
constexpr std::int32_t kFadeNone = 1;
constexpr std::int32_t kFadeOutStart = 2;
constexpr std::int32_t kFadeOutSteps = 3;
constexpr std::int32_t kFadeInBit = 1 << 2;
constexpr std::int32_t kFadeOutComplete = 5;
constexpr std::int32_t kFadeWhite = 1 << 3;

constexpr int kXaLineCount = 727;  // entries of the game's XA line table

void play_effect(int sfx_id, int balance, int volume) {
    sh::audio::game_sound_driver().play_effect(static_cast<std::uint16_t>(sfx_id), static_cast<std::int8_t>(balance),
                                               static_cast<std::uint8_t>(volume));
}

}  // namespace

int disc_file_index(DiscFile file) { return files().find(disc_file_path(file)); }

// ---- fixed-point math ----

std::int32_t sin_q12(std::int32_t angle) { return sh::world::fixed_sin(angle); }
std::int32_t cos_q12(std::int32_t angle) { return sh::world::fixed_cos(angle); }
std::int32_t atan2_q12(std::int32_t y, std::int32_t x) { return sh::world::fixed_atan2(y, x); }

// ---- camera and view ----

void view_point(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t out[3]) {
    const sh::camera::Matrix m =
        sh::camera::world_screen_at(sh::camera::camera_host().world_screen(), sh::camera::Vec3{x, y, z});
    out[0] = m.t[0];
    out[1] = m.t[1];
    out[2] = m.t[2];
}

ViewProjection view_projection() {
    ViewProjection p;
    p.h = sh::math::geometry().screen_distance();
    p.offset_x = sh::math::geometry().screen_offset_x();
    p.offset_y = sh::math::geometry().screen_offset_y();
    return p;
}

void camera_position(std::int32_t out[3]) {
    const sh::camera::Vec3& p = sh::camera::game_camera().state().cam_pos;
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

void camera_rotation(std::int16_t out[9]) {
    const sh::camera::Matrix& m = sh::camera::view_system().world_to_view();
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            out[r * 3 + c] = m.m[r][c];
        }
    }
}

// ---- screen fade ----

bool screen_fade_idle() {
    const std::int32_t state = sh::screens::fade_status() & 0x7;
    return state == kFadeNone || state == kFadeReset;
}

bool screen_fade_finished() { return (sh::screens::fade_status() & 0x7) == kFadeOutComplete; }

void screen_fade_start(bool reset, bool fade_in, bool white) {
    sh::screens::set_fade_status(((reset ? kFadeOutStart : kFadeOutSteps) + (fade_in ? kFadeInBit : 0)) |
                                 (white ? kFadeWhite : 0));
}

void set_screen_fade_timestep(std::int32_t step) { sh::screens::set_fade_timestep(step); }

// ---- frame ----

int tick_count() { return sh::loop::frame_ticks(); }
std::int32_t delta_time() { return sh::loop::frame_delta(); }
std::int32_t delta_time_raw() { return sh::loop::frame_delta_raw(); }
int frame_interval_vblanks() { return sh::loop::frame_interval_vblanks(); }
int frame_buffer_index() { return s_frame_buffer_index; }
void set_frame_buffer_index(int index) { s_frame_buffer_index = index; }
void set_fps_head_offset(int x, int y, int z) {
    sh::screens::set_fps_head_axis(0, x);
    sh::screens::set_fps_head_axis(1, y);
    sh::screens::set_fps_head_axis(2, z);
}

// ---- sound ----

void sfx_play(int sfx_id, int volume) { play_effect(sfx_id, 0, volume); }
void sfx_play_balanced(int sfx_id, int balance, int volume) { play_effect(sfx_id, balance, volume); }
void sound_command(int command) { sh::audio::game_sound_driver().call(static_cast<std::uint32_t>(command)); }

bool xa_line_start_sector(int xa_index, int* sector) {
    const auto& lines = sh::gconfig::active_sound_tables().tables.xa_lines;
    if (xa_index < 0 || xa_index >= kXaLineCount || lines[xa_index].xa_file == 0) {
        return false;
    }
    *sector = static_cast<int>(sh::loader::xa_container_sector(lines[xa_index].xa_file)) + static_cast<int>(lines[xa_index].sector);
    return true;
}

// ---- disc files and the file queue ----

DiscExtent disc_file(DiscFile file) { return extent_of(disc_file_index(file)); }

int file_size(int file_idx) { return static_cast<int>(files().bytes(file_idx)); }
int file_sector_size(int file) { return static_cast<int>(files().sector_bytes(file)); }
int file_queue_length() { return sh::loader::queue_length(); }

void* map_overlay_buffer() { return s_map_overlay_buffer; }
void set_map_overlay_buffer(void* buffer) { s_map_overlay_buffer = buffer; }

ImageHome image_home(ImageSlot slot) {
    const sh::loader::ImagePlace& d = *image_place(slot);
    ImageHome h;
    h.tpage[0] = d.page[0];
    h.tpage[1] = d.page[1];
    h.u = d.u;
    h.v = d.v;
    h.clut_x = d.palette_x;
    h.clut_y = d.palette_y;
    return h;
}

void set_image_home(ImageSlot slot, const ImageHome& home) {
    sh::loader::ImagePlace* d = image_place(slot);
    d->page[0] = home.tpage[0];
    d->page[1] = home.tpage[1];
    d->u = home.u;
    d->v = home.v;
    d->palette_x = home.clut_x;
    d->palette_y = home.clut_y;
}

void queue_tim(DiscFile file, ImageSlot slot) {
    sh::loader::queue_read_image(disc_file_index(file), work_buffer(sh::memory::WorkBuffer::TitleImage), image_place(slot));
}

const char* map_overlay_error() { return sh::loader::active_map_error(); }

// ---- save screens ----

void load_save_screen_files() { sh::app::save_overlay_load(); }

void open_save_screen() {
    SysWork_SavegameUpdatePlayer();
    Screen_Refresh(320, 0);
    sh::app::save_overlay_load();
    sh::loader::queue_wait_empty();
    sh::game::session().set_game_state_next(static_cast<std::int32_t>(GameState::SaveScreen));
}

void open_load_screen() {
    Screen_Refresh(320, 0);
    sh::app::save_overlay_load();
    sh::loader::queue_wait_empty();
    sh::game::session().set_game_state_next(static_cast<std::int32_t>(GameState::LoadSavegameScreen));
}

// ---- inventory ----

void inventory_add(int item_id, int count) {
    Inventory_AddSpecialItem(item_id, count);
}

int inventory_selection() { return static_cast<int>(sh::inventory::inventory_menu().selection); }

void inventory_snap_selection(int id) {
    sh::inventory::InventoryMenuState& menu = sh::inventory::inventory_menu();
    menu.selection = static_cast<std::uint32_t>(id);
    menu.outline_target = id;
    menu.outline_age = 8;
    menu.command = 0;
}

int inventory_command_row() { return sh::inventory::inventory_menu().command; }
void set_inventory_command_row(int row) { sh::inventory::inventory_menu().command = row; }

}  // namespace sh::game_state
