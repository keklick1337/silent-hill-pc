// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Session.audio over sh::game::session() (engine/game_state/session_audio.h): output options,
// the loaded track and ambient bank, the music mix and the state steps the music calls run in. The map's requests
// live in sh::maps::map_runtime().

#include "engine/game_state/session_audio.h"

#include "engine/app/loader/game_files.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/maps/map_runtime.h"

extern "C" {
// The view camera's position and a world point's stereo balance as the camera sees it (the view system).
void vwGetViewPosition(sh::game_state::audio::Point* pos);
std::int8_t Vc_StereoBalanceGet(const sh::game_state::audio::Point* soundPos);
}

namespace sh::game_state::audio {

static_assert(kMusicVolumes == sizeof(sh::game::SessionAudio::music_layer_volumes) / sizeof(std::int16_t));

namespace {
sh::game::Session& s() { return sh::game::session(); }
}  // namespace

bool mono_output() { return s().game.settings.mono != 0; }

int music_track() { return s().game.music_track; }
void set_music_track(int track) { s().game.music_track = track; }
int ambient_bank() { return s().game.ambient_bank; }
void set_ambient_bank(int bank) { s().game.ambient_bank = bank; }
int map_music_track() { return sh::maps::map_runtime().music_track(); }
void set_map_music_track(int track) { sh::maps::map_runtime().set_music_track(track); }
// The map header's ambient bank is a byte the game reads signed.
int map_ambient_bank() { return static_cast<std::int8_t>(sh::maps::map_runtime().ambient_bank()); }
void set_map_ambient_bank(int bank) { sh::maps::map_runtime().set_ambient_bank(bank); }

std::uint32_t music_status() { return s().system.process.music_status; }
void set_music_status(std::uint32_t status) { s().system.process.music_status = status; }
std::int16_t music_layer_volume(int layer) { return s().system.audio.music_layer_volumes[layer]; }
void set_music_layer_volume(int layer, std::int16_t volume) { s().system.audio.music_layer_volumes[layer] = volume; }

int sys_step(int level) { return s().system.flow.sys_steps[level]; }
void set_sys_step(int level, int value) { s().set_sys_step(level, value); }
int game_step(int level) { return s().game.game_steps[level]; }
void set_game_step(int level, int value) { s().game.game_steps[level] = value; }

Point listener_position() {
    Point p;
    vwGetViewPosition(&p);
    return p;
}

int stereo_balance(const Point& source) { return Vc_StereoBalanceGet(&source); }

Point player_position() {
    const sh::entities::Vec3& p = s().system.characters.player.position;
    return {p.x, p.y, p.z};
}

bool radio_noise() { return sh::gameflow::map_event_state().radio_pitch > 0; }
void clear_radio_noise() { sh::gameflow::map_event_state().radio_pitch = 0; }

int map_room_at(std::int32_t x, std::int32_t z) { return sh::maps::map_runtime().room_at(x, z); }
void set_save_room(int room) { sh::game::save_record().room = room; }

std::uint32_t voice_file_sector(int file) { return sh::loader::xa_container_sector(file); }
bool load_queued_chunks() { return sh::loader::queue_load_chunks(); }

}  // namespace sh::game_state::audio
