// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Session.audio: the sound state of the running game as the engine's sound code
// sees it - the music track and ambient bank playing, the music mix (status bits, layer volumes), the map's
// requests, the output options, and where the listener stands. The sound bridges (positional sound, music,
// map-start audio) read and change the game only through here.
//
// Implemented over the session, the map runtime and the engine views in src/audio/game/session_audio.cpp; only
// the map's own music logic (the map runtime's music event) is reached in src/engine/app/game_view_frame.cpp.

#pragma once

#include <cstdint>

namespace sh::game_state::audio {

// A point of the world, Q12 metres (+Y down).
struct Point {
    std::int32_t x = 0, y = 0, z = 0;
};

// Output options.
bool mono_output();

// The listener: the view camera's position, and a world point's stereo balance as the camera sees it (-127 left
// .. 127 right; 0 with mono output).
Point listener_position();
int stereo_balance(const Point& source);
// The player's position.
Point player_position();

// Music track and ambient bank the sound driver has loaded (the game's track numbers; -1 for none).
int music_track();
void set_music_track(int track);
int ambient_bank();
void set_ambient_bank(int bank);
// What the loaded map asks for: its music track and its ambient bank.
int map_music_track();
void set_map_music_track(int track);
int map_ambient_bank();
void set_map_ambient_bank(int bank);

// The music mix: status bits (sh::gameflow::music_status) and the volume of each mix channel (Q12; the last one
// is the ducking channel).
std::uint32_t music_status();
void set_music_status(std::uint32_t status);
constexpr int kMusicVolumes = 9;
std::int16_t music_layer_volume(int layer);
void set_music_layer_volume(int layer, std::int16_t volume);
// The radio picks up something near (its noise pitch is raised); the radio itself may be off.
bool radio_noise();
void clear_radio_noise();

// The map's room at a world point (Q12 x, z), 0 when the map has no rooms; the save's room.
int map_room_at(std::int32_t x, std::int32_t z);
void set_save_room(int room);

// Disc sector where voice file `file` (1..9) starts.
std::uint32_t voice_file_sector(int file);
// Runs the file queue's reads now; true once nothing is left queued.
bool load_queued_chunks();

// The map's own music logic for this frame (`first`: the frame the map started); false when the map has none.
bool run_map_music(bool first);

// Steps of the in-game state (levels 0..2) the music calls run in. Setting a level clears the levels under it
// and their timers, as the game's step changes do.
int sys_step(int level);
void set_sys_step(int level, int value);
// Steps of the top-level screen (levels 0..2), written as they are.
int game_step(int level);
void set_game_step(int level, int value);

}  // namespace sh::game_state::audio
