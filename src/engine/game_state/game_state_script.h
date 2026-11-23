// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the engine's core script binds (src/engine/scripting/game_script_api.cpp) ask the game for:
// the player's control hooks of the loaded map, the flashlight, the event camera,
// the music, the game clock and the screen fade step.
//
// Implemented over the game's records by the game side.
//
// Units are the game's: positions Q12 metres (+Y down), time Q12 seconds.

#pragma once

#include <cstdint>

namespace sh::game_state {

// The loaded map's player control hooks (no-op when the map has none).
void player_control_freeze();
void player_control_unfreeze(bool set_idle);

void set_flashlight(bool on);

// The event camera moves to / looks at a point (Q12) with the seven offset, acceleration and speed words of
// the map's camera call; `warp` jumps there.
void event_camera_position(bool warp, const std::int32_t point[3], const std::int32_t args[7]);
void event_camera_look_at(bool warp, const std::int32_t point[3], const std::int32_t args[7]);

// Music: the next track (the game's track change), a cross-fade to it.
void music_change(int track);
void music_crossfade(int track);

// The frame's game time (Q12 seconds; delta_time() reads it): handlers that pause their own clocks write it.
void set_delta_time(std::int32_t delta);
// The game clock's per-tick advance (play time and the in-game timers).
void game_timer_update();

// Step of the running screen fade per tick (Q12); set_screen_fade_timestep writes it.
std::int32_t screen_fade_timestep();

// The full-screen picture scenes (sh.render screen binds): the display and draw buffers set up again for a
// `width` x 240 (or 480 interlaced) frame, the shown and drawn buffers swapped now, the shortest frame in
// vertical blanks, the presenter's flat-picture mode (4:3 with bars, no hor+), and a background picture drawn
// over the screen this frame (0 the item inspection picture, 1 the paper map; false for another).
void display_init(int width, bool interlaced);
void display_swap();
void set_vblank_interval(int vblanks);
void set_picture_screen(bool on);
bool draw_background_picture(int picture);


}  // namespace sh::game_state
