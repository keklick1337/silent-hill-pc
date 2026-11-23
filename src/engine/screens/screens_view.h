// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screens' view of the running game: the per-frame entry points of the engine
// screens and the state they read. Implemented on the game's records.
#pragma once

#include "engine/screens/cursor_shapes.h"
#include "engine/screens/ranking.h"
#include "engine/screens/screen_fade.h"

#include <cstddef>
#include <cstdint>

namespace sh::screens {

// The game's one fade and one pair of letterbox bars (engine-owned state).
ScreenFade& screen_fade();
CutsceneBorders& cutscene_borders();

// One frame of the fade and of the bars against the game's state; each draws into its slot of the 2D layer.
void run_screen_fade();
void run_cutscene_borders();

// The puzzle cursor and the outline box, at the head of the main 2D list (over the world); the cursor also tells
// the mouse layer that a free cursor is on screen and where.
void draw_puzzle_cursor(const PuzzleCursor& cursor);
void draw_outline_box(std::int16_t t, OutlineBox from, OutlineBox to);

// The end-of-game ranking (engine-owned): evaluated from the running save as the results screen opens; the save of
// the next game takes its carry-over.
Ranking& ranking();
void evaluate_ranking();
void apply_ranking_carry_over();
// The save's attack and kill counters (see ranking.h count_attack, kill_count).
void count_player_attack(int kind, bool hit, std::int32_t distance);
void add_enemy_kill(bool ranged);

// --- What the menu screens (src/ui/screens: options, brightness, controller entry) need of the frame ---

// The words the flow code and the map scripts drive the fade and the background picture with.
struct FadeControl {
    // The fade's status word: state in the low 3 bits, bit 3 a white fade.
    std::int32_t status = 0;
    // The custom fade step (Q12 of the whole fade per frame).
    std::int32_t timestep = 0;
    // The grey of the next background picture (128 = as stored); every picture draw puts it back to 128.
    std::uint8_t background_gamma = 128;
};
FadeControl& fade_control();

// The fade's status word: state in the low 3 bits, bit 3 a white fade.
std::int32_t fade_status();
void set_fade_status(std::int32_t status);

// The 2D order tables: Menu is the menus' 16-entry table, World the main one (back entries first), Hud
// the 16 front slots of the main list, NextBack the back list of the next buffer.
enum class Table2d { Menu, World, Hud, NextBack };
// A 2D command into an order table, the packet cursor moved by the packet the game's drawer built for it.
void submit_2d(Table2d table, int entry, const draw2d::Command& command);

// The packets the game's 2D drawers build (the packet area keeps every later drawer's addresses by their sizes).
enum class Packet2d { PolyF3, PolyF4, PolyG3, PolyG4, PolyFT3, PolyFT4, LineF2, LineG2, LineF4, Tile, Sprite, Sprite8,
                      Sprite16, DrawMode };
std::size_t packet_size(Packet2d packet);
// The packet area's cursor (where the next drawer's packet goes).
void* packet_cursor();
void set_packet_cursor(void* cursor);
// A command placed at `packet` and linked into the order table; the cursor does not move.
void link_2d(Table2d table, int entry, void* packet, const draw2d::Command& command);
// A command into an order table with an explicit packet, the cursor moved by its size.
void submit_2d_packet(Table2d table, int entry, const draw2d::Command& command, Packet2d packet);
// A scene item (an engine-drawn layer) linked at an order table entry.
void link_scene_2d(Table2d table, int entry, std::uint32_t item);
// A draw mode or a fill drawn at once (not through the order tables).
void draw_2d_now(const draw2d::Command& command);
// A rectangle of video memory kept aside in `pixels` (16-bit texels, w x h) and put back, each after the GPU is done.
void keep_vram(int x, int y, int w, int h, std::uint16_t* pixels);
void restore_vram(int x, int y, int w, int h, const std::uint16_t* pixels);

// Pictures the menus keep in video memory.
enum class ScreenImage { ItemInspection, BrightnessFromTitle, BrightnessFromGame, ControllerButtons, CardMessage };
// Reads the disc file `path` (e.g. "TIM/OP_BRT_E.TIM") into the picture's home through the file queue.
void queue_screen_image(const char* path, ScreenImage image);
void wait_for_file_queue();
// The picture as the full-screen background of this frame.
void draw_screen_image(ScreenImage image);

// The sound driver's volumes (0..128): voice stream, music, effects.
void set_sound_volumes(int voice, int music, int effects);
// The game's per-frame chores a screen over the game keeps running.
void update_music_track();
void update_play_timer();
void stop_radio_sound();
// Waits for the GPU to finish the frame, and with `vblanks` for eight vertical blanks too.
void wait_frame_done(bool vblanks);
// Vertical blanks per game frame.
void set_frame_interval(int vblanks);
// The brightness screen's level marks for `level` (0..7).
void draw_brightness_lines(int level);
// One axis (0 x, 1 y, 2 z) of the first-person camera's live eye offset.
void set_fps_head_axis(int axis, int value);

// --- What the movie states (src/ui/screens/movie_screens.cpp) need ---

// The custom fade step (Q12 of the whole fade per frame).
void set_fade_timestep(std::int32_t step);
// Waits for `count` vertical blanks (the game's vertical-blank wait: 0 waits for the next one).
void wait_vblanks(int count);
// Starts the title screen's reads (picture, fonts, the title sounds).
void start_title_loads();
void stop_all_sound_effects();

}  // namespace sh::screens
