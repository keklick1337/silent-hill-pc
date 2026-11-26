// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The engine side of the game-state API (game_state*.h): everything the session, the progress record, the map
// runtime, the camera and the engine's tables answer. The character slots, the engine services and the map header
// are in game_state_characters.cpp, game_state_services.cpp and game_state_overlay.cpp; the calls over the world
// graphics and the draw packets are implemented in src/engine/game_state/game_state_frames.cpp.

#include "engine/game_state/game_state.h"
#include "engine/loader/active_map.h"
#include "engine/loader/map_list.h"
#include "engine/game_state/game_state_app.h"
#include "engine/game_state/game_state_map_overlay.h"
#include "engine/game_state/game_state_ui.h"
#include "engine/game_state/save_record.h"
#include "engine/game_state/session.h"

#include <algorithm>
#include <iterator>

#include "engine/camera/game_camera.h"
#include "engine/gconfig/sound_tables.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/maps/map_runtime.h"
#include "engine/screens/staff_roll_screen.h"

#include <cstring>

namespace sh::game_state {

namespace {

sh::game::Session& s() { return sh::game::session(); }
sh::game::SaveRecord& save() { return sh::game::save_record(); }
sh::game::PadState& pad1() { return s().game.pads[0]; }

constexpr int kInterlacedLines = 448;

// The map loaded now (map registry index), set by the map registry's load.
int g_current_map = kMapIndexFirst;

// Pages of the options screen (its step 0).
constexpr std::int32_t kOptionsBrightness = 3;
constexpr std::int32_t kOptionsEnterBrightness = 8;
constexpr std::int32_t kOptionsLeaveBrightness = 11;

// Inventory commands of an item (the save slot's command byte).
enum InventoryCommand : int {
    kCmdUseHealth = 0, kCmdUse = 1, kCmdEquip = 2, kCmdUnequip = 3, kCmdEquipReload = 4, kCmdUnequipReload = 5,
    kCmdOnOff = 6, kCmdReload = 7, kCmdLook = 8, kCmdUseLook = 9,
};

}  // namespace


// ---- screens and state machines ----

GameState game_state() { return static_cast<GameState>(s().game.game_state); }
SysState sys_state() { return static_cast<SysState>(s().system.flow.sys_state); }
int game_state_raw() { return static_cast<int>(s().game.game_state); }
int sys_state_raw() { return static_cast<int>(s().system.flow.sys_state); }
GameState game_state_previous() { return static_cast<GameState>(s().game.game_state_previous); }
void set_sys_state_next(SysState state) { s().set_sys_state_next(static_cast<std::int32_t>(state)); }

bool demo_active() { return s().sys_flag(sh::game::sys_flag::kDemo); }
void request_warm_reset() { s().system.process.sys_flags |= sh::game::sys_flag::kWarmReset; }

bool options_brightness_open() {
    const std::int32_t step = s().game.game_steps[0];
    return step == kOptionsBrightness || step == kOptionsEnterBrightness;
}
void options_brightness_leave() { s().game.game_steps[0] = kOptionsLeaveBrightness; }

int screen_width() { return s().game.screen_width; }
int screen_height() { return s().game.screen_height; }
bool screen_interlaced() { return s().game.screen_height == kInterlacedLines; }

// ---- pad 1 ----

bool pad_binds(std::uint16_t out[kPadBindCount]) {
    const sh::game::PadBinds& b = s().game.settings.binds;
    const std::uint16_t binds[] = {b.enter, b.cancel, b.skip,       b.action,     b.aim,   b.light, b.run,
                                   b.view,  b.step_left, b.step_right, b.pause, b.item,  b.map,   b.option};
    static_assert(std::size(binds) == kPadBindCount);
    std::copy(std::begin(binds), std::end(binds), out);
    return true;
}

bool pad_held(std::uint32_t* out) {
    *out = pad1().held;
    return true;
}

bool pad_clicked(std::uint32_t* out) {
    *out = pad1().clicked;
    return true;
}

// The stick the menus read is the previous-frame copy (the record's second stick block).
bool pad_left_stick(int* x, int* y) {
    *x = pad1().sticks_past_dead_zone.left_x;
    *y = pad1().sticks_past_dead_zone.left_y;
    return true;
}

void set_pad_left_stick(int x, int y) {
    pad1().sticks_past_dead_zone.left_x = static_cast<std::int8_t>(x);
    pad1().sticks_past_dead_zone.left_y = static_cast<std::int8_t>(y);
}

void pad_press(std::uint32_t buttons) { pad1().clicked |= buttons; }

void pad_press_held(std::uint32_t buttons) {
    pad1().clicked |= buttons;
    pad1().held |= buttons;
}

std::uint32_t pad_stick_left_bit() { return pad::kStickLeft; }
std::uint32_t pad_stick_right_bit() { return pad::kStickRight; }

// ---- player combat and the NPC bookkeeping ----

bool player_aiming() { return s().system.combat.aiming != 0; }
int player_weapon_attack() { return s().system.combat.weapon_attack; }

// ---- the progress record ----

bool save_active() { return true; }
int save_map_index() { return save().map; }
int save_room() { return save().room; }
int current_map() { return g_current_map; }
void set_current_map_index(int map_index) { g_current_map = map_index; }
int save_difficulty() { return save().difficulty(); }
void set_save_difficulty(int difficulty) { save().set_difficulty(difficulty); }
const std::uint32_t* save_event_flags() { return save().event_flags; }
static_assert(kEventFlagWords == sh::game::SaveRecord::kEventFlagWords);
int save_flag_words() { return save().flag_words(); }
std::uint32_t save_flag_word(int word) { return save().flag_word(word); }

bool event_flag(int flag) { return save().event_flag(flag); }
void set_event_flag(int flag, bool on) { save().set_event_flag(flag, on); }

bool inventory_has(int item_id) {
    for (const sh::game::SaveItem& it : save().items)
        if (it.item == item_id)
            return true;
    return false;
}

void unlock_hyperblaster() { save().endings |= sh::game::save_bits::kEndingUfo; }

bool paper_map_held(int idx) { return ((save().paper_maps >> idx) & 1u) != 0; }
void grant_paper_map(int idx) { save().paper_maps |= 1u << (idx % 32); }
int save_paper_map() { return save().paper_map; }
void set_save_paper_map(int idx) { save().paper_map = idx; }

void set_health_saturation(std::int32_t saturation) { save().health_saturation = saturation; }
bool radio_on() { return (save().item_toggles & sh::game::save_bits::kRadioOn) != 0; }

int inventory_slot_count() { return save().slot_count; }
int inventory_screen_step() { return s().game.game_steps[1]; }
int inventory_selected_item() { return s().system.inventory_selected; }
int inventory_equipped_item() { return s().system.combat.weapon_slot; }
// The kitchen knife's tap attack: the first entry of the attack numbering.
int inventory_first_weapon_attack() { return 0; }

int inventory_command_count(int inventory_idx) {
    switch (save().items[inventory_idx].command) {
        case kCmdUseHealth:
        case kCmdUse:
        case kCmdEquip:
        case kCmdUnequip:
        case kCmdReload:
        case kCmdLook:
            return 1;
        case kCmdEquipReload:
        case kCmdUnequipReload:
        case kCmdOnOff:
        case kCmdUseLook:
            return 2;
        default:
            return 0;
    }
}

// ---- camera, staff roll, sound tables ----

CameraDebug camera_debug() {
    const auto& st = sh::camera::game_camera().state();
    CameraDebug c;
    c.position[0] = st.cam_pos.x;
    c.position[1] = st.cam_pos.y;
    c.position[2] = st.cam_pos.z;
    c.look_at[0] = st.watch_target_pos.x;
    c.look_at[1] = st.watch_target_pos.y;
    c.look_at[2] = st.watch_target_pos.z;
    c.pitch = st.cam_rotation.x;
    c.yaw = st.cam_rotation.y;
    c.roll = st.cam_rotation.z;
    c.screen_distance = st.geom_screen_dist;
    return c;
}

// ---- maps ----

int map_count() { return sh::loader::map_count(); }
const char* map_name(int map_index) { return sh::loader::map_name(map_index); }
const char* map_description(int map_index) { return sh::loader::map_description(map_index); }
int map_find(const char* name) { return sh::loader::find_map(name); }
const char* map_overlay_name() { return sh::loader::active_map_name(); }
int map_overlay_map_type() { return sh::maps::map_runtime().map_type(); }

}  // namespace sh::game_state
