// SPDX-License-Identifier: GPL-3.0-or-later
//
// The frame and world parts of engine/app/game_view.h, the loader's view (engine/app/loader/game_files_view.h) and
// the map music hook (engine/game_state/session_audio.h): the display double buffer and the frame's order layers,
// the world environment's fog, the position log of the debug keys, and the loads the file queue reports back. Over
// the session, the camera, the atmosphere, the frame layers and the renderer's display calls by their engine names.

#include <cstddef>
#include <cstdint>

#include "engine/app/game_view.h"
#include "engine/app/loader/game_files_view.h"
#include "engine/camera/game_camera.h"
#include "engine/chara/game/chara_file_table.h"
#include "engine/entities/game/entity_host.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/game_state/session_audio.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_runtime.h"
#include "engine/math/geometry_state.h"
#include "engine/runtime/cell_stream_runtime.h"
#include "engine/screens/staff_roll_clock.h"
#include "engine/world/game/character_world.h"
#include "engine/world/world_atmosphere.h"
#include "render/frame/frame.h"
#include "render/gpu/game_display.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/gameflow/anim_slots.h"

extern "C" {
extern unsigned char* GsOUT_PACKET_P;    // the frame's packet cursor
// The lake and sewer water drawers (src/engine/effects/game/water_host.cpp).
void shLensFlareDraw(void);
// The free camera's eye (src/engine/app/devtools/free_camera_host.cpp): Q12 x, y, z.
void Pc_FreeCam_Position(std::int32_t* out);
// The running map event's flag bits (src/engine/gameflow/game/map_events_host.cpp).
std::int8_t GameState_UnkS8Get(void);
// The sound driver's XA line (src/audio/game/game_sound_driver.cpp).
std::uint16_t Sd_GetXaAudioIdx(void);
void Sd_ForceClearXaAudioIdx(void);
// A cutscene's file from the importer's output (src/engine/scripting/cutscene_calls.cpp).
int PcCutscene_FileRead(void* dest, int file);
}

namespace {

sh::world::AtmosphereState& atmosphere() { return sh::world::detail::g_atmosphere_state; }

}  // namespace

namespace sh::app::view {

void water_update() { shLensFlareDraw(); }
bool fog_enabled() { return atmosphere().fog_enabled != 0; }
void set_fog_enabled(bool on) { atmosphere().fog_enabled = on ? 1 : 0; }
void fog_color(std::uint8_t rgb[3])
{
    rgb[0] = atmosphere().fog.color.r;
    rgb[1] = atmosphere().fog.color.g;
    rgb[2] = atmosphere().fog.color.b;
}
const char* map_message(std::int32_t index) { return sh::maps::map_runtime().messages()[index]; }

void log_player_position(bool free_camera)
{
    std::int32_t cam[3];
    if (free_camera) {
        Pc_FreeCam_Position(cam);
    } else {
        const sh::camera::Vec3& p = sh::camera::game_camera().state().cam_pos;
        cam[0] = p.x;
        cam[1] = p.y;
        cam[2] = p.z;
    }
    const sh::entities::CharacterRecord& p = sh::game::session().system.characters.player;
    const sh::world::game::SurfaceRecord here = sh::world::game::surface_at(p.position.x, p.position.z);
    const sh::game::SaveRecord& save = sh::game::save_record();
    const sh::camera::Angles& cam_rot = sh::camera::game_camera().state().cam_rotation;
    constexpr std::int32_t kNoGround = 8 * 4096;  // the surface query's height where there is no ground
    SH_DBG("HARRY POSITION LOGGED mapId=%d roomIdx=%d pos=(%ld,%ld,%ld) yaw=%d pitch=%d moveSpeed=%ld camPos=(%ld,%ld,%ld) camYaw=%d camPitch=%d groundH=%ld slopeX=%d slopeZ=%d validPts=%d voidCell=%d",
           (int)save.map, (int)save.room, (long)p.position.x, (long)p.position.y, (long)p.position.z,
           (int)p.rotation.y, (int)p.rotation.x, (long)p.move_speed, (long)cam[0], (long)cam[1], (long)cam[2],
           (int)cam_rot.y, (int)cam_rot.x, (long)here.height, (int)here.slope_x, (int)here.slope_z,
           (int)here.ground_type, (int)(here.height == kNoGround));
}

void geometry_init() { sh::math::geometry().reset(); }

int frame_buffer_begin()
{
    sh::game_state::set_frame_buffer_index(sh::gpu::display::active_buffer());
    return sh::game_state::frame_buffer_index();
}

void frame_lists_clear(void* packets)
{
    GsOUT_PACKET_P = static_cast<unsigned char*>(packets);
    sh::gpu::display::clear_layer(sh::frame::Layer::World);
    sh::gpu::display::clear_layer(sh::frame::Layer::Overlay);
}

std::ptrdiff_t packets_used(const void* base) { return GsOUT_PACKET_P - static_cast<const unsigned char*>(base); }
void sort_clear(std::uint8_t r, std::uint8_t g, std::uint8_t b) { sh::gpu::display::sort_clear(r, g, b); }
void prepare_world_list() { sh::gpu::display::prepare_world_layer(); }
void draw_world_list() { sh::gpu::display::draw_layer(sh::frame::Layer::World); }
void draw_ui_list() { sh::gpu::display::draw_layer(sh::frame::Layer::Overlay); }
void swap_buffers() { sh::gpu::display::swap_buffers(); }
void set_projection_offset(std::int32_t x, std::int32_t y) { sh::math::geometry().set_screen_offset(x, y); }
void item_depth_begin() { sh::gpu::display::item_depth_begin(); }
void item_depth_end() { sh::gpu::display::item_depth_end(); }

int map_event_flags() { return GameState_UnkS8Get(); }

void audio_state_force_clear()
{
    SH_DBG("[SH] load watchdog: forcing audio state clear (xaIdx %d)", (int)Sd_GetXaAudioIdx());
    Sd_ForceClearXaAudioIdx();
}

int load_flag_tick() { return ++sh::gameflow::map_event_state().load_flag_ticks; }

}  // namespace sh::app::view

namespace sh::loader::view {

void entity_file_loaded(std::int32_t file, void* data) { sh::entities::game::anim_file_loaded(file, data); }
bool cutscene_file_read(void* dest, std::int32_t file) { return PcCutscene_FileRead(dest, file) != 0; }
void anim_file_loaded(int slot, int chara_id, void* data, void* bones)
{
    sh::gameflow::anim_slots().update(slot, chara_id, data, bones);
}
std::int32_t chara_anim_file(int chara_id) { return sh::chara::game::chara_file_row(chara_id).anim; }
void chunk_load_mark() { sh::screens::staff_roll_clock() = 1; }
void pool_write_probe(int x, int y, int w, int h) { sh::world::stream::pool_write_probe(x, y, w, h); }

}  // namespace sh::loader::view

namespace sh::game_state::audio {

bool run_map_music(bool first)
{
    const sh::maps::MapRuntime::MusicEvent event = sh::maps::map_runtime().music_event();
    if (event == nullptr)
        return false;
    event(first);
    return true;
}

}  // namespace sh::game_state::audio
