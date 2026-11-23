// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The state of the running game as the PC layer sees it: which screen is
// up, the player and the NPC slots, the inventory and the save flags, the loaded map, the menu sounds. The
// console, the debug overlay, mouse picking and the PC screens read and change the game only through here; none
// of them sees the game's own records.
//
// The functions the session, the progress record and the engine answer are implemented in game_state.cpp; the ones
// over the character slots are in game_state_characters.cpp, the ones over the engine's services (math, camera,
// fade, sound, file queue) in game_state_services.cpp. The rest (the map overlay header, the world graphics, the
// draw packets) are implemented in src/engine/game_state/game_state_frames.cpp.
//
// Units are the game's: positions Q12 metres (+Y down), angles Q12 turns (4096 = 360 degrees), health Q12.

#pragma once

#include "engine/game_state/game_ids.h"

#include "engine/entities/character.h"

#include <cstdint>

namespace sh::entities {
class EntityWorld;
}

namespace sh::game_state {

// Top-level screen (the game's state machine); the values are the game's.
enum class GameState : int {
    Init = 0,
    KonamiLogo = 1,
    KcetLogo = 2,
    MovieIntroFadeIn = 3,
    AutoLoadSavegame = 4,
    MovieIntroAlternate = 5,
    MovieIntro = 6,
    MainMenu = 7,
    LoadSavegameScreen = 8,
    MovieOpening = 9,
    MainLoadScreen = 10,
    InGame = 11,
    MapEvent = 12,
    ExitMovie = 13,
    InventoryScreen = 14,
    PaperMapScreen = 15,
    SaveScreen = 16,
    DebugMoviePlayer = 17,
    OptionScreen = 18,
    LoadStatusScreen = 19,
    LoadMapScreen = 20,
};

// What the in-game loop is doing (only meaningful while GameState is InGame / MapEvent).
enum class SysState : int {
    Gameplay = 0,
    OptionsMenu = 1,
    StatusMenu = 2,
    MapScreen = 3,
    Fmv = 4,
    LoadOverlay = 5,
    LoadRoom = 6,
    ReadMessage = 7,
    SaveMenu0 = 8,
    SaveMenu1 = 9,
    EventCallback = 10,
    EventSetFlag = 11,
    EventPlaySound = 12,
    GameOver = 13,
    GamePaused = 14,
};

GameState game_state();
SysState sys_state();
// The in-game state the next tick switches to (the game's state change, with its step reset).
void set_sys_state_next(SysState state);
// The previous top-level screen.
GameState game_state_previous();
// Real time of the last tick (Q12 seconds), unaffected by slow motion.
std::int32_t delta_time_raw();
// Raw values for logs (out-of-range states print as numbers).
int game_state_raw();
int sys_state_raw();

// The player's button binds (bits of the pad word), in the game's settings order.
enum PadBind : int {
    kPadEnter, kPadCancel, kPadSkip, kPadAction, kPadAim, kPadLight, kPadRun, kPadView,
    kPadStepLeft, kPadStepRight, kPadPause, kPadItem, kPadMap, kPadOption, kPadBindCount
};
// Copies the binds into `out`; false (out untouched) before the game has its settings.
bool pad_binds(std::uint16_t out[kPadBindCount]);
// Buttons held on pad 1 this frame; false before the game reads pads.
bool pad_held(std::uint32_t* out);
// Buttons that went down on pad 1 this frame; false before the game reads pads.
bool pad_clicked(std::uint32_t* out);
// Pad word bits of the directions (digital pad and left stick).
namespace pad {
constexpr std::uint32_t kDpadUp = 1u << 4, kDpadRight = 1u << 5, kDpadDown = 1u << 6, kDpadLeft = 1u << 7;
constexpr std::uint32_t kStickUp = 1u << 24, kStickRight = 1u << 25, kStickDown = 1u << 26, kStickLeft = 1u << 27;
constexpr int kStickThreshold = 64;  // half deflection of the left stick (-128..127)
}  // namespace pad
// Pad 1's left stick (-128..127 per axis), and overrides of this frame's pad record for the menus and puzzles
// the mouse drives: the stick, and extra pressed buttons. False / no-op before the game reads pads.
bool pad_left_stick(int* x, int* y);
void set_pad_left_stick(int x, int y);
void pad_press(std::uint32_t buttons);

// The game's frame size (320 or 640 wide; 224 or 448 lines).
int screen_width();
int screen_height();

// A file of the disc image by its role, for the code that reads disc bytes (fan text, fonts).
enum class DiscFile : int {
    Font16Tim,        // 1ST/FONT16.TIM
    OptionOverlay,    // VIN/OPTION.BIN
    SaveLoadOverlay,  // VIN/SAVELOAD.BIN
    TitleTim,         // TIM/TITLE.TIM (the Japanese title picture)
    TitleETim,        // TIM/TITLE_E.TIM (the US title picture)
    WarningTim,       // 1ST/2ZANKO_E.TIM (the boot warning picture)
};
struct DiscExtent {
    std::uint32_t start_sector = 0;
    std::uint32_t size_bytes = 0;  // whole 256-byte blocks
};
DiscExtent disc_file(DiscFile file);
// Its index in the disc file table (-1 when the table has no such file).
int disc_file_index(DiscFile file);

// Images in video memory: where the game keeps one (its image descriptor: texture page, u/v in it, palette),
// and uploads of TIM images to such a home.
struct ImageHome {
    std::uint8_t tpage[2] = {0, 0};  // the descriptor's two page bytes, as the game stores them
    std::uint8_t u = 0, v = 0;
    std::int16_t clut_x = 0, clut_y = 0;
};
enum class ImageSlot : int { Title, Font16Atlas };
ImageHome image_home(ImageSlot slot);
void set_image_home(ImageSlot slot, const ImageHome& home);
// Uploads the TIM file image `tim` (in memory) to `home`: pixels to its page, palette to its clut, then waits for
// the copy. `patch` may edit the pixels before (16-bit words: width in words, height in lines). False when the
// data is not a TIM.
bool upload_tim(void* tim, const ImageHome& home, void (*patch)(void* pixels, int width_words, int height) = nullptr);
// Reads a disc image into the home of `slot` through the game's file queue (asynchronous, like the game's own).
void queue_tim(DiscFile file, ImageSlot slot);

// Boot frames: full-screen pictures shown before the game's own screens (the PC warning screen), on the game's
// display in its 640x448 interlaced mode, centre origin. The caller builds a frame with boot_frame_quad /
// boot_frame_fade and shows it with boot_frame_present.
void boot_frame_begin();                                       // display mode set, first frame cleared
void boot_frame_load_tim(DiscFile file, const ImageHome& home);  // reads the image and waits for it
void boot_frame_show();                                        // display on, 60 fps frame pacing
// A textured quad of an 8-bit image on texture page `page` (64-word columns), palette (clut_x, clut_y).
struct BootQuad {
    std::int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // corners: top left, bottom right
    std::uint8_t u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    int page = 0;
    int clut_x = 0, clut_y = 0;
};
void boot_frame_quad(const BootQuad& quad);
// Darkens the whole frame by `fade` (0..255, subtractive), over the quads.
void boot_frame_fade(int fade);
// Waits for the vertical blank, shows the frame and draws it; `next` also starts an empty next frame.
void boot_frame_present(bool next);

// The game's menu sound effects (ids of the sound bank).
namespace sfx {
constexpr int kMenuError = 1304;
constexpr int kMenuMove = 1305;
constexpr int kMenuCancel = 1306;
constexpr int kMenuConfirm = 1307;
}  // namespace sfx
// Plays a sound effect centred at `volume` (0..127; the menus use 64): the game's sound call.
void sfx_play(int sfx_id, int volume);
// Queues a sound command (an effect id) through the game's sound task queue, as the game's own menus do.
void sound_command(int command);

// Text colours of the game's string drawer (ui/menu/nm_draw.h and the menus).
namespace text_color {
constexpr int kGold = 0;
constexpr int kDarkGrey = 1;
constexpr int kGreen = 2;
constexpr int kRed = 4;
constexpr int kLightGrey = 6;
constexpr int kWhite = 7;
}  // namespace text_color

// The characters of the running map: slot 0 the player, 1..kNpcSlots the NPCs (sh::entities).
constexpr int kNpcSlots = 120; // == NPC_COUNT_MAX; the first 32 are the room spawner's slots
sh::entities::EntityWorld& characters();

// Light reads and writes of one slot for per-frame code (no Character refresh). An empty or out-of-range slot
// reads chara::kNone / 0.
int chara_id(int slot);
std::int32_t chara_health(int slot);
void set_chara_health(int slot, std::int32_t health);
// The health still to come back from an ampoule (Q12).
void set_health_saturation(std::int32_t saturation);
sh::entities::Vec3 chara_position(int slot);
sh::entities::Angles chara_rotation(int slot);

// Writes of one slot (the console's teleports and kills).
void set_chara_position(int slot, const sh::entities::Vec3& position);
void set_chara_rotation_y(int slot, std::int16_t yaw);
// A hit of `amount` the character applies on its own next update, through its own death path.
void chara_take_damage(int slot, std::int32_t amount);

// A character type in the loaded map: its model streamed, its animation slot resident (-1 when not), the map's
// update for it.
bool chara_model_loaded(int chara_id);
int chara_anim_slot(int chara_id);
bool chara_has_behaviour(int chara_id);

// The game's fixed-point math (Q12 angles and results).
std::int32_t sin_q12(std::int32_t angle);
std::int32_t cos_q12(std::int32_t angle);
std::int32_t atan2_q12(std::int32_t y, std::int32_t x);

// The behaviour state of a slot (control state and its step).
void chara_control(int slot, int* state, int* step);

// The ground under (x, z) through the world's ground query (Q12 height, +Y down).
std::int32_t ground_height(std::int32_t x, std::int32_t z);
// The full ground sample: height, slope and ground type.
struct GroundSample {
    std::int32_t height = 0;
    int tilt_x = 0, tilt_z = 0;
    int type = 0;
};
GroundSample ground_sample(std::int32_t x, std::int32_t z);
// Puts the player at (x, z) on the ground there, or at height *y when `y` is given; the fall stops.
void place_player(std::int32_t x, std::int32_t z, const std::int32_t* y);

// Debug view of the player's animation (the debug overlay's animation panel).
// The player's animation state (packed status, flags, control state and step, upper-body state).
struct PlayerAnimDebug {
    int status = 0, flags = 0, control = 0, step = 0, upper_body = 0;
};
PlayerAnimDebug player_anim_debug();
// Keyframe range of Harry's base animation `idx` (false for blend entries or past the table).
constexpr int kHarryBaseAnims = 256;
bool harry_anim_range(int idx, int* start, int* end);

// A recorded attract demo is playing (it drives the pad itself).
bool demo_active();
// Asks the game for a warm reset to the title.
void request_warm_reset();
// The options screen's brightness page is up (or opening); leaving it goes back to the options list.
bool options_brightness_open();
void options_brightness_leave();

// The camera of the frame being drawn: a world point (Q12) in view space (x right, y down, z depth; geometry
// units, 256 = 1 m), and the perspective of the projection (distance H, screen offset in prim coordinates).
void view_point(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t out[3]);
struct ViewProjection {
    std::int32_t h = 0;
    std::int32_t offset_x = 0, offset_y = 0;
};
ViewProjection view_projection();
// The camera itself: position (Q12 world) and world -> view rotation (Q12, row major).
void camera_position(std::int32_t out[3]);
void camera_rotation(std::int16_t out[9]);

// The player's weapon state: aiming, and the packed attack (weapon id + input type * 10; guns start at
// kFirstGunAttack, melee and unarmed are below).
bool player_aiming();
int player_weapon_attack();
constexpr int kFirstGunAttack = 32;  // Handgun, tap

// 2D overlay drawn over the world this frame (the layer the cutscene borders use): quads in centre-origin screen
// coordinates (x 0 = screen centre, 320x224 space), vertex order 0 1 / 2 3, one flat colour, half-translucent
// (0.5 * src + 0.5 * dst). At most kOverlayQuadsMax quads per frame.
struct OverlayQuad {
    std::int16_t x[4] = {0, 0, 0, 0};
    std::int16_t y[4] = {0, 0, 0, 0};
};
constexpr int kOverlayQuadsMax = 12;
void overlay_translucent_quads(const OverlayQuad* quads, int count, std::uint8_t r, std::uint8_t g, std::uint8_t b);

// The frame of the menus: 448 lines (title and difficulty menus, interlaced) or 224 (in game).
bool screen_interlaced();

// A textured quad on the menu layer (drawn over the menus' own text), prim coordinates (centre origin),
// vertex order 0 1 / 2 3. The texture is a 4-bit image whose page starts at VRAM x `texture_x` (y 0), palette at
// (clut_x, clut_y); `layer` orders it within the menu layer (higher draws later).
struct TexturedQuad {
    std::int16_t x[4] = {0, 0, 0, 0};
    std::int16_t y[4] = {0, 0, 0, 0};
    std::uint8_t u[4] = {0, 0, 0, 0};
    std::uint8_t v[4] = {0, 0, 0, 0};
    int texture_x = 0;
    int clut_x = 0, clut_y = 0;
    std::uint8_t r = 128, g = 128, b = 128;
};
void menu_textured_quad(int layer, const TexturedQuad& quad);

// Save and load screens, opened the way the save point and the inventory do (screen assets read first).
void open_save_screen();
void open_load_screen();

// The running save (the game's savegame record): absent before New Game / Load.
bool save_active();
int save_map_index();   // map index of the current map (the map registry's numbering)
int save_difficulty();  // action difficulty: -1 easy, 0 normal, 1 hard
void set_save_difficulty(int difficulty);  // same values; debug console only

// The event table of the loaded map (the overlay's triggers).
struct MapEvent {
    int trigger_type = 0;
    int activation_type = 0;
    int sys_state = 0;  // SysState the event switches to
    int param = 0;      // handler slot for SysState::EventCallback
    int point = 0;      // map point it happens at
    int required_flag = 0;
    int disabled_flag = 0;
};
bool map_events_loaded();
int map_event_count();  // entries before the end mark (at most 256, the game's cap)
MapEvent map_event(int idx);
// Moves the player onto the map point of event `idx` (no-op without a point table).
void map_event_place_player(int idx);
// Starts event `idx` the way its trigger does: a set-flag event sets its flag; any other becomes the current
// event and its system state the next one.
void map_event_start(int idx);
// No screen fade running.
bool screen_fade_idle();
// Game ticks since boot.
int tick_count();
// Name of the loaded map overlay, nullptr for none.
const char* map_overlay_name();

// The map loaded now (map registry index) and the save's room in it.
int current_map();
int save_room();
// The save's event flags, 32 per word (kEventFlagWords words); nullptr before a save exists.
constexpr int kEventFlagWords = 52;
const std::uint32_t* save_event_flags();

// Inventory and save progress.
bool inventory_has(int item_id);       // any of the table's entries
void inventory_add(int item_id, int count);  // the game's special-item add (stacks what stacks)
// The clear-ending bit the HyperBlaster's fire test reads (UFO ending).
void unlock_hyperblaster();
bool event_flag(int flag);
void set_event_flag(int flag, bool on);
// Area maps: held bits and the area the player is in.
constexpr int kPaperMaps = 24;
constexpr int kPaperMapOtherPlaces = 0;  // the tag of areas without a map
bool paper_map_held(int idx);
void grant_paper_map(int idx);
int save_paper_map();

// Screen fade: the current fade finished; start one (to black unless `white`; `fade_in` reveals the picture).
bool screen_fade_finished();
void screen_fade_start(bool reset, bool fade_in, bool white);
// Step of the running fade per tick (Q12); 0 lets the file queue pace it.
void set_screen_fade_timestep(std::int32_t step);
// Reads the save/load screen's files (before switching to it).
void load_save_screen_files();

// The first sound effect id.
constexpr int kSfxBase = 1280;

// The view camera for debug marks: position, look-at point (Q12), angles and projection distance.
struct CameraDebug {
    std::int32_t position[3] = {0, 0, 0};
    std::int32_t look_at[3] = {0, 0, 0};
    int pitch = 0, yaw = 0, roll = 0;
    int screen_distance = 0;
};
CameraDebug camera_debug();

// The map registry: map index <-> overlay name ("map0_s00") and its description.
int map_count();
const char* map_name(int map_index);         // nullptr when out of range
const char* map_description(int map_index);  // nullptr when out of range
int map_find(const char* name);              // -1 when unknown

// Empties NPC slot `slot` (1..kNpcSlots). False when it was already empty.
bool despawn_npc(int slot);
// Game time of the last tick (Q12 seconds), scaled by slow motion.
std::int32_t delta_time();
// The player's radio is switched on.
bool radio_on();

// The animation file of a character type, -1 when it has none.
int chara_anim_file(int chara_id);
// Size of a disc file rounded up to whole sectors.
int file_sector_size(int file);
// The item in the player's hand.
int held_item_id();
bool held_item_hidden();

}  // namespace sh::game_state
