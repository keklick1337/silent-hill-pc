// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The engine's core sh.* binds: event flags, items, the player, the event steps, input,
// the event camera, timers, music state, screen fades, maths, random numbers, keyed text and the map's
// world objects, over engine classes (sh::game::session(), sh::game_state accessors, sh::loop::GameRandom,
// sh::loader::DiscFiles). Registered first (order 0) through ShScript_RegisterGameBinder; the game's
// other bridges and the event binds register after it.

#include "engine/loader/active_map.h"
#include "engine/runtime/world_object_binds.h"
#include "engine/script/sh_script.h"

#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_inventory.h"
#include "engine/game_state/game_state_script.h"
#include "engine/game_state/session.h"
#include "engine/loader/queue/disc_files.h"
#include "engine/loop/game_random.h"
#include "engine/runtime/game_hooks.h"
#include "engine/script/host_interfaces.h"
#include "i18n/text_bridge.h"
#include "render/gpu/renderer.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

namespace gs = sh::game_state;

sh::game::Session& session() { return sh::game::session(); }

// ---- Event flags (the save's flag words) ----

int flag_get(int id) { return gs::event_flag(id) ? 1 : 0; }
void flag_set(int id, int value) { gs::set_event_flag(id, value != 0); }
// The map-event "complete/disabled" write: the same bit as a plain set.
void flag_set_alt(int id) { gs::set_event_flag(id, true); }

class Flags final : public sh::script::FlagsHost {
public:
    int get(int id) override { return flag_get(id); }
    void set(int id) override { flag_set(id, 1); }
    void clear(int id) override { flag_set(id, 0); }
    void set_alt(int id) override { flag_set_alt(id); }
};

// ---- Items (the live save's inventory slots up to its slot count) ----

int held_slot(int item) {
    const int shown = gs::inventory_shown_slots();
    for (int i = 0; i < shown && i < static_cast<int>(sh::inventory::kSlotCapacity); ++i) {
        const sh::inventory::Slot slot = gs::inventory_slot(i);
        if (slot.item == item && slot.count > 0) {
            return i;
        }
    }
    return -1;
}

int item_count(int item) {
    const int i = held_slot(item);
    return i >= 0 ? gs::inventory_slot(i).count : 0;
}

void take_item(int item, int count) {
    const int i = held_slot(item);
    if (i < 0) {
        return;
    }
    sh::inventory::Slot slot = gs::inventory_slot(i);
    slot.count = slot.count > count ? slot.count - count : 0;
    gs::set_inventory_slot(i, slot);
}

// Adds to the inventory without a pickup; the item.given hook may change the count or cancel it.
void give_item(int item, int count) {
    const int given = sh::hooks::game_item_given(item, count, "script");
    if (given > 0) {
        gs::inventory_add(item, given);
    }
}

// ---- The player (slot 0) ----

constexpr int kPlayer = 0;

void player_pos(int out[3]) {
    const sh::entities::Vec3 p = gs::chara_position(kPlayer);
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
}

void player_set_pos(const int xyz[3]) { gs::set_chara_position(kPlayer, {xyz[0], xyz[1], xyz[2]}); }

void player_freeze(int on) {
    if (on != 0) {
        gs::player_control_freeze();
    } else {
        gs::player_control_unfreeze(false);
    }
}

void player_unfreeze_set_idle(int set_idle) { gs::player_control_unfreeze(set_idle != 0); }
void flashlight(int on) { gs::set_flashlight(on != 0); }

int player_rot_y_get() { return gs::chara_rotation(kPlayer).y; }
void player_rotate(int angle) { gs::set_chara_rotation_y(kPlayer, static_cast<std::int16_t>(angle)); }

int player_vel_y_get() { return gs::chara_position(kPlayer).y; }

void player_vel_y_set(int y) {
    sh::entities::Vec3 p = gs::chara_position(kPlayer);
    p.y = y;
    gs::set_chara_position(kPlayer, p);
}

// The hatch falls and climbs move the player 0.3 m/s down (+1) or up (-1): a 64-bit Q12 product.
void player_fall_tick(int direction) {
    constexpr std::int32_t kRate = 1228;  // Q12(0.3), truncated
    const std::int32_t rate = direction < 0 ? -kRate : kRate;
    sh::entities::Vec3 p = gs::chara_position(kPlayer);
    p.y += static_cast<std::int32_t>((static_cast<std::int64_t>(gs::delta_time()) * rate) >> 12);
    gs::set_chara_position(kPlayer, p);
}

int player_weapon_attack_get() { return session().system.combat.weapon_attack; }
void player_weapon_attack_set(int attack) { session().system.combat.weapon_attack = attack; }

class Player final : public sh::script::PlayerHost {
public:
    void pos(int out[3]) override { player_pos(out); }
    void set_pos(const int xyz[3]) override { player_set_pos(xyz); }
    void freeze(int on) override { player_freeze(on); }
    void unfreeze(int set_idle) override { player_unfreeze_set_idle(set_idle); }
    void flashlight(int on) override { ::flashlight(on); }
};

// ---- NPC slots 1..kNpcSlots (the cutscenes warp and re-task them) ----

bool npc_slot(int slot) { return slot >= 1 && slot <= gs::kNpcSlots; }

void chara_slot_transform_get(int slot, int out[4]) {
    if (!npc_slot(slot)) {
        return;
    }
    const sh::entities::Vec3 p = gs::chara_position(slot);
    out[0] = p.x;
    out[1] = p.y;
    out[2] = p.z;
    out[3] = gs::chara_rotation(slot).y;
}

void chara_slot_transform_set(int slot, const int xyz[3], int rot_y) {
    if (!npc_slot(slot)) {
        return;
    }
    gs::set_chara_position(slot, {xyz[0], xyz[1], xyz[2]});
    gs::set_chara_rotation_y(slot, static_cast<std::int16_t>(rot_y));
}

int chara_slot_chara_id_get(int slot) { return npc_slot(slot) ? gs::chara_id(slot) : -1; }

// ---- The in-game state machine and its steps ----

constexpr int kSteps = 3;

void state_set(int sys_state) { session().set_sys_state_next(sys_state); }
int event_step_get(int level) { return level >= 0 && level < kSteps ? session().system.flow.sys_steps[level] : 0; }

void event_step_set(int level, int value) {
    if (level >= 0 && level < kSteps) {
        session().system.flow.sys_steps[level] = value;
    }
}

void event_step_increment(int level) { session().advance_sys_step(level); }
void event_step_reset() { session().set_sys_step(0, -1); }

// The plate cut-scenes ramp their cross-fade alpha in the data word of step 0.
int step_data_get() { return session().system.flow.sys_step_data[0]; }
void step_data_set(int value) { session().system.flow.sys_step_data[0] = value; }

// The high nibble of the text flags: "yes" is pre-selected and confirms without a sound.
void selection_silent(int on) {
    std::uint8_t& flags = session().system.text_flags;
    flags = static_cast<std::uint8_t>((flags & 0x0F) | ((on != 0 ? 1 : 0) << 4));
}

void cutscene_border(int state) { session().system.flow.cutscene_border = state; }
void cutscene_flag(int on) { session().set_sys_flag(sh::game::sys_flag::kCutscene, on != 0); }

// ---- Time ----

int delta_time_get() { return gs::delta_time(); }
void delta_time_set(int value) { gs::set_delta_time(value); }
void delta_time_reset() { gs::set_delta_time(0); }

// Each map library defines its own cutscene clock; it is looked up in the loaded map.
std::int32_t* cutscene_timer() { return static_cast<std::int32_t*>(sh::loader::active_map_symbol("g_Cutscene_Timer")); }

class Timers final : public sh::script::TimerHost {
public:
    // The system timer is the data word of step 1.
    int sys_get() override { return session().system.flow.sys_step_data[1]; }
    void sys_set(int v) override { session().system.flow.sys_step_data[1] = v; }
    int cutscene_get() override {
        const std::int32_t* t = cutscene_timer();
        return t != nullptr ? *t : 0;
    }
    void cutscene_set(int v) override {
        if (std::int32_t* t = cutscene_timer()) {
            *t = v;
        }
    }
    void game_update() override { gs::game_timer_update(); }
};

// ---- Screen fades ----

void fade_start(int fade_in, int white) { gs::screen_fade_start(true, fade_in != 0, white != 0); }
int fade_done() { return gs::screen_fade_finished() ? 1 : 0; }
int screen_fade_timestep_get() { return gs::screen_fade_timestep(); }
void screen_fade_timestep_set(int value) { gs::set_screen_fade_timestep(value); }

// ---- Input: the player's binds on pad 1 ----

const std::uint16_t* bind_of(const char* action) {
    sh::game::PadBinds& b = session().game.settings.binds;
    const struct {
        const char* name;
        std::uint16_t* bits;
    } kBinds[] = {
        {"enter", &b.enter},   {"cancel", &b.cancel},          {"skip", &b.skip},
        {"action", &b.action}, {"aim", &b.aim},                {"light", &b.light},
        {"run", &b.run},       {"view", &b.view},              {"stepLeft", &b.step_left},
        {"stepRight", &b.step_right}, {"pause", &b.pause},     {"item", &b.item},
        {"map", &b.map},       {"option", &b.option},
    };
    for (const auto& e : kBinds) {
        if (std::strcmp(e.name, action) == 0) {
            return e.bits;
        }
    }
    return nullptr;
}

// mode: 0 held, 1 newly pressed, 2 released, 3 pulsed (auto-repeat).
int input_action(const char* action, int mode) {
    const std::uint16_t* bits = action != nullptr ? bind_of(action) : nullptr;
    if (bits == nullptr) {
        return 0;
    }
    const sh::game::PadState& pad = session().game.pads[0];
    std::uint32_t state = pad.clicked;
    switch (mode) {
        case 0: state = pad.held; break;
        case 2: state = pad.released; break;
        case 3: state = pad.pulsed; break;
        default: break;
    }
    return (state & *bits) != 0 ? 1 : 0;
}

// stick 0: this frame's sticks, else last frame's; axis 0 left x, 1 left y, 2 right x, 3 right y.
int input_axis(int stick, int axis) {
    const sh::game::PadState& pad = session().game.pads[0];
    const sh::game::PadSticks& s = stick == 0 ? pad.sticks : pad.sticks_past_dead_zone;
    switch (axis) {
        case 0: return s.left_x;
        case 1: return s.left_y;
        case 2: return s.right_x;
        default: return s.right_y;
    }
}

// ---- Event camera ----

void camera_pos(int warp, int pos[3], int args[7]) { gs::event_camera_position(warp != 0, pos, args); }
void camera_look(int warp, int look[3], int args[7]) { gs::event_camera_look_at(warp != 0, look, args); }

class Camera final : public sh::script::CameraHost {
public:
    void pos(int warp, int p[3], int args[7]) override { camera_pos(warp, p, args); }
    void look(int warp, int p[3], int args[7]) override { camera_look(warp, p, args); }
};

// ---- Music ----

class Music final : public sh::script::MusicHost {
public:
    void change(int track) override { gs::music_change(track); }
    void crossfade(int track) override { gs::music_crossfade(track); }
    int status_flags() override { return static_cast<int>(session().system.process.music_status); }
    void set_status_flags(int v) override { session().system.process.music_status = static_cast<std::uint32_t>(v); }
};

// ---- Maths and random numbers (the game's tables and generator, bit-exact) ----

int math_sin(int angle) { return gs::sin_q12(angle); }
int math_cos(int angle) { return gs::cos_q12(angle); }
int math_atan2(int y, int x) { return gs::atan2_q12(y, x); }
int rng_int(int low, int high) { return sh::loop::game_random().next_int(low, high); }
int rng_uint(int low, int high) { return sh::loop::game_random().next_uint(low, high); }

// ---- Files and text ----

int file_number(const char* path) { return path != nullptr ? sh::loader::disc_files().find(path) : -1; }
const char* text_get(const char* key) { return Pc_TextGet(key); }
int msg_index(const char* key) { return Pc_TextMsgIndex(key); }

// ---- The full-screen picture scenes (sh.render screen binds, sh.paper_map_grant) ----

constexpr int kVramWidth = 1024, kVramHeight = 512;

// Copies of video memory pieces a scene keeps while its picture covers them ("screen_backup", "spare").
std::vector<std::uint16_t> g_vram_copies[2];

int refuse(char* err, int cap, const char* why);

bool vram_rect(const ShRenderVramCopy* spec, sh::gpu::Rect* rect, char* err, int cap) {
    if (spec == nullptr || spec->buffer < 0 || spec->buffer >= static_cast<int>(std::size(g_vram_copies))) {
        refuse(err, cap, "unknown buffer");
        return false;
    }
    if (spec->x < 0 || spec->y < 0 || spec->w <= 0 || spec->h <= 0 || spec->x + spec->w > kVramWidth ||
        spec->y + spec->h > kVramHeight) {
        refuse(err, cap, "rect leaves VRAM");
        return false;
    }
    *rect = sh::gpu::Rect{static_cast<std::int16_t>(spec->x), static_cast<std::int16_t>(spec->y),
                          static_cast<std::int16_t>(spec->w), static_cast<std::int16_t>(spec->h)};
    return true;
}

int vram_save(const ShRenderVramCopy* spec, char* err, int cap) {
    sh::gpu::Rect rect;
    if (!vram_rect(spec, &rect, err, cap)) {
        return -1;
    }
    std::vector<std::uint16_t>& copy = g_vram_copies[spec->buffer];
    copy.assign(static_cast<std::size_t>(spec->w) * static_cast<std::size_t>(spec->h), 0);
    if (sh::gpu::Renderer* r = sh::gpu::renderer()) {
        r->store_image(rect, copy.data());
    }
    return 0;
}

int vram_restore(const ShRenderVramCopy* spec, char* err, int cap) {
    sh::gpu::Rect rect;
    if (!vram_rect(spec, &rect, err, cap)) {
        return -1;
    }
    std::vector<std::uint16_t>& copy = g_vram_copies[spec->buffer];
    copy.resize(static_cast<std::size_t>(spec->w) * static_cast<std::size_t>(spec->h), 0);
    if (sh::gpu::Renderer* r = sh::gpu::renderer()) {
        r->load_image(rect, copy.data());
    }
    return 0;
}

int display_init(int width, int interlaced, char* err, int cap) {
    if (width < 256 || width > 640) {
        return refuse(err, cap, "bad width");
    }
    gs::display_init(width, interlaced != 0);
    return 0;
}

void display_swap() { gs::display_swap(); }
void vblank_interval(int vblanks) { gs::set_vblank_interval(vblanks); }
void picture_screen(int on) { gs::set_picture_screen(on != 0); }

int background(int picture, char* err, int cap) {
    return gs::draw_background_picture(picture) ? 0 : refuse(err, cap, "unknown texture");
}

void paper_map_grant(int idx) {
    if (idx >= 0 && idx < 32) {
        gs::grant_paper_map(idx);
    }
}

int refuse(char* err, int cap, const char* why) {
    std::snprintf(err, static_cast<std::size_t>(cap), "%s", why);
    return -1;
}

// ---- Registration ----

Flags g_flags;
Player g_player;
Camera g_camera;
Timers g_timers;
Music g_music;

void bind_core(ShScriptGameApi* api) {
    sh::script::set_flags_host(&g_flags);
    sh::script::set_player_host(&g_player);
    sh::script::set_camera_host(&g_camera);
    sh::script::set_timer_host(&g_timers);
    sh::script::set_music_host(&g_music);

    api->flag_get = flag_get;
    api->flag_set = flag_set;
    api->flag_set_alt = flag_set_alt;
    api->give_item = give_item;
    api->item_count = item_count;
    api->take_item = take_item;
    api->map_name = nullptr;  // the host answers sh.map_name itself
    api->player_pos = player_pos;
    api->player_set_pos = player_set_pos;
    api->player_freeze = player_freeze;
    api->player_unfreeze_set_idle = player_unfreeze_set_idle;
    api->flashlight = flashlight;
    api->player_rot_y_get = player_rot_y_get;
    api->player_rotate = player_rotate;
    api->player_vel_y_get = player_vel_y_get;
    api->player_vel_y_set = player_vel_y_set;
    api->player_fall_tick = player_fall_tick;
    api->player_weapon_attack_get = player_weapon_attack_get;
    api->player_weapon_attack_set = player_weapon_attack_set;
    api->chara_slot_transform_get = chara_slot_transform_get;
    api->chara_slot_transform_set = chara_slot_transform_set;
    api->chara_slot_chara_id_get = chara_slot_chara_id_get;
    api->state_set = state_set;
    api->event_step_get = event_step_get;
    api->event_step_set = event_step_set;
    api->event_step_increment = event_step_increment;
    api->event_step_reset = event_step_reset;
    api->state_timer_get = step_data_get;
    api->state_timer_set = step_data_set;
    api->selection_silent = selection_silent;
    api->cutscene_border = cutscene_border;
    api->cutscene_flag = cutscene_flag;
    api->delta_time_get = delta_time_get;
    api->delta_time_set = delta_time_set;
    api->delta_time_reset = delta_time_reset;
    api->fade_start = fade_start;
    api->fade_done = fade_done;
    api->screen_fade_finished = fade_done;
    api->screen_fade_timestep_get = screen_fade_timestep_get;
    api->screen_fade_timestep_set = screen_fade_timestep_set;
    api->input_action = input_action;
    api->input_axis = input_axis;
    api->camera_pos = camera_pos;
    api->camera_look = camera_look;
    api->math_sin = math_sin;
    api->math_cos = math_cos;
    api->math_atan2 = math_atan2;
    api->rng_int = rng_int;
    api->rng_uint = rng_uint;
    api->file_number = file_number;
    api->text_get = text_get;
    api->msg_index = msg_index;
}

void bind_screen(ShScriptGameApi* api) {
    api->render_vram_save = vram_save;
    api->render_vram_restore = vram_restore;
    api->render_display_init = display_init;
    api->render_display_swap = display_swap;
    api->render_vblank_interval = vblank_interval;
    api->render_picture_screen = picture_screen;
    api->render_background = background;
    api->paper_map_grant = paper_map_grant;
}

// sh.world_object.* wraps the named API, which the game's bridges set up (order 60 and before).
void bind_world_objects(ShScriptGameApi* api) { sh::world::bind_world_object_api(api); }

const ShScriptGameBinderEntry g_core_binder(0, bind_core);
const ShScriptGameBinderEntry g_screen_binder(121, bind_screen);
const ShScriptGameBinderEntry g_world_objects_binder(130, bind_world_objects);

}  // namespace
