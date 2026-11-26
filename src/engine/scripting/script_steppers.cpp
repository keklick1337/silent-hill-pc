// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game binders that hand sh.* the event steppers, sounds, character loads, world objects of the map and the
// named API, and the accessors of engine/game_state/game_state_script.h. Everything goes
// through engine state (the session, the map event state, the map variables) or the C entry points of the engine's
// own hosts and of other modules (script_game_link.h, named_api_calls.h); registered through ShScript_RegisterGameBinder
// after the engine's core binds (src/engine/scripting/game_script_api.cpp).

#include "engine/loader/active_map.h"
#include "engine/ids/game_numbers.h"
#include "engine/chara/chara_script.h"
#include "engine/chara/runtime/chara_data.h"
#include "engine/game_state/game_state_script.h"
#include "engine/game_state/session.h"
#include "engine/gameflow/game/game_link.h"
#include "engine/gameflow/event_triggers.h"
#include "engine/gameflow/map_event_state.h"
#include "engine/app/game_view.h"
#include "engine/runtime/game_hooks.h"
#include "engine/script/host_interfaces.h"
#include "engine/script/named_api.h"
#include "engine/script/named_api_calls.h"
#include "engine/script/sh_script.h"
#include "engine/scripting/script_buffers.h"
#include "engine/scripting/script_game_link.h"
#include "engine/app/loader/game_files.h"
#include "engine/scripting/cutscene_calls.h"

#include <cstring>

extern int g_PcMapScreenActive;  // src/engine/app/loop/main_loop.cpp

extern "C" {
// The player overlay and the map's world objects.
void Player_ControlFreeze(void);
void Player_ControlUnfreeze(std::uint32_t set_idle);
// Music and sound (src/audio/game).
void Bgm_TrackChange(std::int32_t track);
void Bgm_CrossfadeToTrack(std::int32_t track);
void SD_Call(std::uint32_t cmd);
void Sd_SfxStop(std::uint16_t sfx);
std::uint8_t Sd_PlaySfx(std::uint16_t sfx, std::int8_t balance, std::uint8_t volume);
void Sd_SfxAttributesUpdate(std::uint16_t sfx, std::int8_t balance, std::uint8_t volume, std::int8_t pitch);
std::uint8_t Sd_AudioStreamingCheck(void);
void Sd_XaPreLoadAudioPreTaskAdd(std::uint16_t line);
std::uint32_t Sd_AmbientSfxInit(void);
void Sfx_WithFlagsPlay(int sfx, const std::int32_t* position, std::int32_t volume, std::int32_t flags);
void Sfx_WithFlagsAndPitchPlay(int sfx, const std::int32_t* position, std::int32_t volume, std::int32_t flags,
                               std::int32_t pitch);
void Sfx_WithPitchPlay(int sfx, const std::int32_t* position, std::int32_t volume, std::int8_t pitch);
void Sfx_WithFalloffAndPitchPlay(int sfx, std::int32_t* position, std::int32_t volume, std::int32_t falloff,
                                 std::int8_t pitch);
void Sfx_Play3dWithFalloff(int sfx, std::int32_t* position, std::int32_t volume, std::int32_t falloff, std::int8_t pitch);
void Game_RadioSoundStop(void);
void Radio_NpcInfoClear(void);
void Bgm_PlayNewTrack(std::int32_t track);
// Event helpers of the engine's hosts (src/engine/gameflow/game/event_helpers_host.cpp).
void Event_ItemTake(std::int32_t item, std::int32_t count, std::int32_t flag, std::int32_t message);
void Event_CommonItemTake(std::uint32_t pickup, std::int32_t flag);
void Event_DisplayMapMsgWithBg(std::int32_t file, std::int32_t fade0, std::int32_t fade1, std::int32_t message);
void Event_PaperMapTake(std::int32_t paper_map, std::int32_t flag, std::int32_t message);
void Event_BgTextureFadeIn(std::int32_t file, std::int32_t fade0, std::int32_t fade1);
void Event_PathWaypointExecuteChara(void* chara, std::int32_t anim, std::int32_t count, std::uint32_t sub_step);
// Pictures and the screen.
void Screen_BackgroundImgTransition(void* from, void* to, std::int32_t alpha);
// Held items and the warm boot (src/engine/app/game_view.cpp).
void WorldGfx_HeldItemAttach(std::int32_t chara, std::int32_t bone);
std::int32_t WorldGfx_PlayerHeldItemSet(std::int32_t item);
void Game_WarmBoot(void);
// Chara_Spawn and Chara_Load of the NPC spawn host (src/engine/gameflow/game/npc_spawn_host.cpp).
void Chara_SpawnFlagsSet(std::int32_t chara, std::int32_t spawn, std::int32_t flags);
}

namespace {

namespace gg = sh::gameflow::game;
namespace link = sh::scripting::link;
using sh::script::named_calls::CBool;

constexpr int kSfxBase = 1280;                   // sound ids of the scripts start past the base effects
constexpr std::int32_t kEventSoundHeight = -4915;  // Q12(-1.2 m): the shared door handlers' sound height
constexpr int kMessageSelectFirst = 1;          // map message: the first entry of a choice taken
constexpr int kMessageFinished = -1;            // map message: done (NO_VALUE)
constexpr std::uint32_t kVoiceDialog = 1u << 5;  // music status: a voiced dialogue plays
constexpr int kLegacyNpcSlots = 16;             // the play-to-end bind reaches the first 16 NPC slots only

sh::entities::CharacterRecord* chara(int slot) {
    return sh::game::session().system.characters.record(slot);
}

}  // namespace

/* ---- engine/game_state/game_state_script.h ---- */

namespace sh::game_state {

void player_control_freeze() {
    Player_ControlFreeze();
}

void player_control_unfreeze(bool set_idle) {
    Player_ControlUnfreeze(set_idle ? 1u : 0u);
}

void set_flashlight(bool on) {
    if (on) {
        Game_TurnFlashlightOn();
    } else {
        Game_TurnFlashlightOff();
    }
}

void event_camera_position(bool warp, const std::int32_t point[3], const std::int32_t args[7]) {
    std::int32_t at[3] = {point[0], point[1], point[2]};
    Event_CameraPositionSet(at, args[0], args[1], args[2], args[3], args[4], args[5], args[6], warp ? 1 : 0);
}

void event_camera_look_at(bool warp, const std::int32_t point[3], const std::int32_t args[7]) {
    std::int32_t at[3] = {point[0], point[1], point[2]};
    Event_CameraLookAtSet(at, args[0], args[1], args[2], args[3], args[4], args[5], args[6], warp ? 1 : 0);
}

void music_change(int track) { Bgm_TrackChange(track); }
void music_crossfade(int track) { Bgm_CrossfadeToTrack(track); }
void set_delta_time(std::int32_t delta) { link::set_frame_value(link::FrameValue::DeltaTime, delta); }
void game_timer_update() { Game_TimerUpdate(); }

std::int32_t screen_fade_timestep() {
    return static_cast<std::int32_t>(link::frame_value(link::FrameValue::ScreenFadeTimestep));
}

void display_init(int width, bool interlaced) { sh::app::view::screen_init_mode(width, interlaced); }
void display_swap() { gg::swap_display_buffers(); }
void set_vblank_interval(int vblanks) { gg::set_interval_vblanks(vblanks); }
void set_picture_screen(bool on) { g_PcMapScreenActive = on ? 1 : 0; }

bool draw_background_picture(int picture) {
    switch (picture) {
        case 0:
            gg::draw_picture(gg::Picture::ItemInspection);
            return true;
        case 1:
            // The paper map palette lies inside the shown frame's VRAM rectangle, which the frame store writes back
            // every frame: the sheet uploads the TIM again from its buffer first.
            gg::draw_paper_map_sheet();
            return true;
        default:
            return false;
    }
}

}  // namespace sh::game_state

/* ---- The steppers behind sh.* ---- */

namespace {

// The active event's map point at the shared door handlers' sound height.
void event_point_position(std::int32_t out[3]) {
    out[0] = 0;
    out[1] = kEventSoundHeight;
    out[2] = 0;
    const void* event = sh::gameflow::map_event_state().event;
    if (event != nullptr && gg::map_points_present()) {
        const sh::gameflow::MapPointRecord point = gg::map_point(gg::map_event(event).point);
        out[0] = point.x;
        out[2] = point.z;
    }
}

const std::int32_t* point_or_null(const int* p, std::int32_t* buf) {
    if (p == nullptr) {
        return nullptr;
    }
    buf[0] = p[0];
    buf[1] = p[1];
    buf[2] = p[2];
    return buf;
}

// A scripted map event rides the contract the trigger scan leaves behind: the gameplay dispatcher runs it on its
// next tick like a data event. The record is the map data's 12-byte event entry: the required and the done flag,
// the trigger byte, point, item, and a word of sysState (5 bits), param (8) and map (6, from bit 25).
struct EventRecord {
    std::int16_t required_flag;
    std::int16_t done_flag;
    std::uint8_t trigger;
    std::uint8_t point;
    std::uint8_t item;
    std::uint8_t reserved;
    std::uint32_t word;
};
static_assert(sizeof(EventRecord) == 12);

EventRecord g_SyntheticEvent;

void map_event_set(int sys_state, int param, int map, int complete_flag) {
    g_SyntheticEvent = {};
    g_SyntheticEvent.required_flag = -1;
    g_SyntheticEvent.done_flag = static_cast<std::int16_t>(complete_flag);
    g_SyntheticEvent.word = (static_cast<std::uint32_t>(sys_state) & 0x1F) |
                            ((static_cast<std::uint32_t>(param) & 0xFF) << 5) |
                            ((static_cast<std::uint32_t>(map) & 0x3F) << 25);
    sh::gameflow::MapEventState& state = sh::gameflow::map_event_state();
    if (state.sys_state == static_cast<std::int32_t>(sh::gameflow::GameState::None) ||
        state.sys_state == static_cast<std::int32_t>(sh::gameflow::GameState::Gameplay)) {
        // No event is running (the game's own script, a coroutine): the next trigger scan would overwrite it.
        state.queued_event = &g_SyntheticEvent;
        state.queued_sys_state = sys_state;
        state.queued_param = static_cast<std::uint32_t>(param);
        return;
    }
    state.event = &g_SyntheticEvent;
    state.sys_state = sys_state;
    state.param = static_cast<std::uint32_t>(param);
}

int event_point() {
    const void* event = sh::gameflow::map_event_state().event;
    return event != nullptr ? gg::map_event(event).point : -1;
}

int event_activation_type_get() {
    const void* event = sh::gameflow::map_event_state().event;
    return event != nullptr ? gg::map_event(event).activation : -1;
}

void event_msg(int has_selection, int msg, int step0, int step1, int step2, int sub_step) {
    Event_DisplayMapMsg(has_selection != 0, msg, step0, step1, step2, sub_step != 0);
}

void event_msg_with_sfx(int msg, int sfx) {
    std::int32_t at[3];
    event_point_position(at);
    Event_DisplayMapMsgWithSfx(msg, sfx, at);
}

// The map message with the caller's voice command: the handler passes the current voice command and moves on when
// the returned state is "finished".
int event_msg_with_audio(int msg, int voice_cmd) {
    sh::game::session().system.process.music_status |= kVoiceDialog;
    gg::set_message_game_voiced(true);
    const int state = Gfx_MapMsg_Draw(msg);
    gg::set_message_game_voiced(false);

    if (state == kMessageSelectFirst) {
        sh::game::session().advance_sys_step(0);
    } else if (state == kMessageFinished) {
        if (voice_cmd >= 0x1000 && voice_cmd < 0x1700) {
            sh::gameflow::map_event_state().voice_dropped = 0;
            SD_Call(static_cast<std::uint16_t>(voice_cmd));
        } else {
            sh::gameflow::map_event_state().voice_dropped = 1;
        }
    }
    return state;
}

// A world object record of the map by its storage name (the draw record the world objects keep).
struct ObjectRecord {
    char* base = nullptr;
    link::ObjectRecordLayout layout;
    std::int32_t* position() const { return reinterpret_cast<std::int32_t*>(base + layout.position); }
    std::int16_t* rotation() const { return reinterpret_cast<std::int16_t*>(base + layout.rotation); }
};

ObjectRecord world_object(const char* name) {
    return {static_cast<char*>(sh::loader::active_map_symbol(name)), link::object_record_layout()};
}

void world_object_add(const char* name) {
    const ObjectRecord obj = world_object(name);
    if (obj.base == nullptr) {
        return;
    }
    const std::int16_t rot[3] = {0, 0, 0};
    link::object_record_draw(obj.base, obj.position(), rot);
}

int world_object_pos_get(const char* name, int out[3]) {
    const ObjectRecord obj = world_object(name);
    if (obj.base == nullptr) {
        return 0;
    }
    std::memcpy(out, obj.position(), 3 * sizeof(std::int32_t));
    return 1;
}

int world_object_pos_set(const char* name, const int xyz[3]) {
    const ObjectRecord obj = world_object(name);
    if (obj.base == nullptr) {
        return 0;
    }
    std::memcpy(obj.position(), xyz, 3 * sizeof(std::int32_t));
    return 1;
}

int world_object_rot_y_set(const char* name, int rot_y) {
    const ObjectRecord obj = world_object(name);
    if (obj.base == nullptr) {
        return 0;
    }
    obj.rotation()[1] = static_cast<std::int16_t>(rot_y);
    return 1;
}

int world_object_rot_y_get(const char* name) {
    const ObjectRecord obj = world_object(name);
    return obj.base != nullptr ? obj.rotation()[1] : 0;
}

void chara_anim_play_to_end(int slot, int anim_state) {
    if (slot == 0 || slot - 1 < kLegacyNpcSlots) {
        if (sh::entities::CharacterRecord* c = chara(slot)) {
            Event_CharaAnimPlayToEnd(c, anim_state);
        }
    }
}

void chara_slot_control_set(int slot, int control_state, int state_step) {
    if (slot < 1 || slot > sh::entities::kNpcSlotCount) {
        return;
    }
    sh::entities::CharacterRecord* c = chara(slot);
    c->model.control_state = static_cast<std::uint8_t>(control_state);
    c->model.control_step = static_cast<std::uint8_t>(state_step);
}

void chara_slot_anim_flags_set(int slot, int flags) {
    if (sh::entities::CharacterRecord* c = chara(slot)) {
        c->model.anim.flags |= static_cast<std::uint16_t>(flags);
    }
}

void chara_slot_anim_flags_clear(int slot, int flags) {
    if (sh::entities::CharacterRecord* c = chara(slot)) {
        c->model.anim.flags &= static_cast<std::uint16_t>(~flags);
    }
}

// The SD bus commands the scripts call by name (the numbered bank-load ranges are not commands).
int sd_command_number(const char* name) {
    static constexpr struct {
        const char* name;
        int command;
    } kCommands[] = {
        {"mono", 1},          {"stereo", 2},           {"mute", 3},        {"unmute", 4},
        {"stop_all_sfx", 16}, {"stop_all_sfx_rr", 17}, {"stop_bgm", 18},   {"stop_xa_audio", 19},
        {"stop_all", 20},     {"stop_all_rr", 21},     {"bgm_fade_1", 22}, {"bgm_fade_2", 23},
    };
    for (const auto& entry : kCommands) {
        if (std::strcmp(entry.name, name) == 0) {
            return entry.command;
        }
    }
    return -1;
}

class Sound final : public sh::script::SoundHost {
public:
    void play(int id) override { SD_Call((static_cast<std::uint32_t>(static_cast<std::uint16_t>(id)) + kSfxBase) & 0xFFFF); }
    void stop(int id) override { Sd_SfxStop(static_cast<std::uint16_t>(id)); }
    void play_3d(int id, int vol) override {
        std::int32_t at[3];
        event_point_position(at);
        Sfx_WithFlagsPlay(id, at, vol, 0);
    }
    void play_3d_at(int id, int x, int y, int z, int vol) override {
        const std::int32_t at[3] = {x, y, z};
        Sfx_WithFlagsPlay(id, at, vol, 0);
    }
    void play_flags(int id, int x, int y, int z, int vol, int f) override {
        const std::int32_t at[3] = {x, y, z};
        Sfx_WithFlagsPlay(id, at, vol, f);
    }
    void play_flags_pitch(int id, int x, int y, int z, int vol, int f, int p) override {
        const std::int32_t at[3] = {x, y, z};
        Sfx_WithFlagsAndPitchPlay(id, at, vol, f, p);
    }
    void play_pitch(int id, int x, int y, int z, int vol, int p) override {
        const std::int32_t at[3] = {x, y, z};
        Sfx_WithPitchPlay(id, at, vol, static_cast<std::int8_t>(p));
    }
    void play_at_pos(int id, int x, int y, int z, int vol, int p) override { play_pitch(id, x, y, z, vol, p); }
    void play_falloff_pitch(int id, int x, int y, int z, int vol, int fo, int p) override {
        std::int32_t at[3] = {x, y, z};
        Sfx_WithFalloffAndPitchPlay(id, at, vol, fo, static_cast<std::int8_t>(p));
    }
    void sd_call(int cmd) override { SD_Call(static_cast<std::uint32_t>(cmd)); }
    int sd_command_number(const char* name) override { return ::sd_command_number(name); }
    void attributes_update(int id, int bal, int vol, int p) override {
        Sd_SfxAttributesUpdate(static_cast<std::uint16_t>(id), static_cast<std::int8_t>(bal),
                               static_cast<std::uint8_t>(vol), static_cast<std::int8_t>(p));
    }
    int play_balanced(int id, int bal, int vol) override {
        return Sd_PlaySfx(static_cast<std::uint16_t>(id), static_cast<std::int8_t>(bal), static_cast<std::uint8_t>(vol));
    }
    int position_ref(const char* ref, int out[3], char* err, int err_cap) override {
        return PcScriptNamed_PositionRef(ref, out, err, err_cap);
    }
    void play_with_flags(int id, const int* pos, int vol, int flags) override {
        std::int32_t at[3];
        Sfx_WithFlagsPlay(id, point_or_null(pos, at), vol, flags);
    }
    void play_falloff(int id, const int* pos, int vol, int falloff, int pitch) override {
        std::int32_t at[3];
        Sfx_WithFalloffAndPitchPlay(id, const_cast<std::int32_t*>(point_or_null(pos, at)), vol, falloff,
                                    static_cast<std::int8_t>(pitch));
    }
    void play_3d_typed(int id, const int* pos, int vol, int sound_type) override {
        std::int32_t at[3];
        Sfx_WithFlagsPlay(id, point_or_null(pos, at), vol, sound_type);
    }
    void play_3d_falloff(int id, int x, int y, int z, int vol, int falloff, int pitch) override {
        std::int32_t at[3] = {x, y, z};
        Sfx_Play3dWithFalloff(id, at, vol, falloff, static_cast<std::int8_t>(pitch));
    }
    int streaming() override { return Sd_AudioStreamingCheck(); }
    void xa_preload(int id) override { Sd_XaPreLoadAudioPreTaskAdd(static_cast<std::uint16_t>(id)); }
    int ambient_ready() override { return Sd_AmbientSfxInit() != 0 ? 1 : 0; }
    void radio_stop() override { Game_RadioSoundStop(); }
    void radio_npc_clear() override { Radio_NpcInfoClear(); }
};

class Spawn final : public sh::script::SpawnHost {
public:
    int spawn(int c, int f, int x, int z, int r, int s) override {
        return Chara_Spawn(c, f, x, z, static_cast<std::int16_t>(r), static_cast<std::uint32_t>(s));
    }
    void spawn_flags_set(int c, int i, int f) override { Chara_SpawnFlagsSet(c, i, f); }
    void spawn_entry_set(int entry, int on) override {
        if (entry < 0 || entry >= 32) {
            return;
        }
        std::int32_t& groups = sh::game::session().system.scene.spawned_groups;
        if (on != 0) {
            groups |= static_cast<std::int32_t>(1u << entry);
        } else {
            groups &= ~static_cast<std::int32_t>(1u << entry);
        }
    }
};

Sound g_Sound;
Spawn g_Spawn;

void bind_old_steppers(ShScriptGameApi* api) {
    sh::script::set_sound_host(&g_Sound);
    sh::script::set_spawn_host(&g_Spawn);
    sh::hooks::install_game_script_hosts();  // sh.game/world/entity queries of mods

    api->msg_draw = [](int msg) { return static_cast<int>(Gfx_MapMsg_Draw(msg)); };
    api->map_event_set = map_event_set;
    api->event_point = event_point;
    api->event_item_take = [](int item, int count, int flag, int msg) { Event_ItemTake(item, count, flag, msg); };
    api->event_common_item_take = [](int pickup, int flag) {
        Event_CommonItemTake(static_cast<std::uint32_t>(pickup), flag);
    };
    api->item_number = sh::ids::item_number;
    api->flag_number = sh::ids::flag_number;
    api->paper_map_number = sh::ids::paper_map_number;
    api->common_pickup_number = sh::ids::common_pickup_number;
    api->event_msg = event_msg;
    api->event_selection = event_msg;
    api->event_msg_with_sfx = event_msg_with_sfx;
    api->event_msg_with_bg = [](int file, int fade0, int fade1, int msg) {
        Event_DisplayMapMsgWithBg(file, fade0, fade1, msg);
    };
    api->event_paper_map_take = [](int paper_map, int flag, int msg) { Event_PaperMapTake(paper_map, flag, msg); };
    api->event_msg_with_texture = [](int file, int fade0, int fade1, int msg0, int msg1) {
        Event_DisplayMapMsgWithDimmedBg(file, fade0, fade1, msg0, msg1);
    };
    api->background_gamma = [](int gamma) { link::set_frame_value(link::FrameValue::BackgroundGamma, gamma); };
    // The steppers advance the state machine themselves when done.
    api->event_wait_timer = [](int duration, int reset) {
        Event_WaitTimer(duration, reset != 0);
        return 1;
    };
    api->event_step_increment_delayed = [](int delay, int reset) { Event_WaitTimer(delay, reset != 0); };
    api->event_wait_player_stop = []() {
        Event_WaitPlayerStop();
        return 1;
    };
    api->event_fade_cmd = [](int cmd, int fade_out, int fade_type, int timestep, int sub_step) {
        Event_ScreenFadeCmd(cmd, fade_out != 0, fade_type, timestep, sub_step != 0);
    };
    api->event_step_increment_after_fade = api->event_fade_cmd;
    // The plate inserts cross-fade the inspection picture to the plate-in-slot one.
    api->bg_img_transition = [](int which, int alpha) {
        if (which == 0) {
            Screen_BackgroundImgTransition(link::picture_record(link::Picture::ItemInspection),
                                           link::picture_record(link::Picture::EventSecondary), alpha);
        }
    };
    api->camera_auto_return = [](int warp) { vcReturnPreAutoCamWork(warp != 0); };
    api->world_object_add = world_object_add;
    api->event_bg_texture_cmd = [](int cmd, int file, int sub_step) { Event_BgTextureCmd(cmd, file, sub_step != 0); };
    api->overlay_screen_set = api->event_bg_texture_cmd;
    api->event_inv_item_cmd = [](int cmd, int item, int count, int sub_step) {
        Event_InvItemCmd(cmd, item, count, sub_step != 0);
    };
    api->pickup_animate = [](int item) { return Gfx_PickupItemAnimate(item) ? 1 : 0; };
    api->chara_anim_play_to_end = chara_anim_play_to_end;
    api->chara_slot_control_set = chara_slot_control_set;
    api->event_msg_with_audio = event_msg_with_audio;
    api->event_activation_type_get = event_activation_type_get;
    api->event_tween_linear = [](int target, int duration, int timer) {
        return static_cast<int>(Event_TweenLinear(target, duration, timer));
    };
    api->event_tween_reset = [](int timer) { Event_TweenReset(timer); };
    api->chara_slot_anim_flags_set = chara_slot_anim_flags_set;
    api->chara_slot_anim_flags_clear = chara_slot_anim_flags_clear;
    api->water_effect_enable = []() { Water_EffectEnable(); };
    api->water_effect_disable = []() { Water_EffectDisable(); };
    api->event_sys_state_branch_on_flag = [](int flag, int step_true, int step_false, int set_sub_step) {
        Event_SysStateBranchOnFlag(flag, step_true, step_false, set_sub_step != 0);
    };
    api->held_item_show = []() { WorldGfx_HeldItemShow(); };
    api->held_item_hide = []() { WorldGfx_HeldItemHide(); };
    api->world_object_pos_get = world_object_pos_get;
    api->world_object_pos_set = world_object_pos_set;
    api->world_object_rot_y_set = world_object_rot_y_set;
    api->world_object_rot_y_get = world_object_rot_y_get;
    api->held_item_attach = [](int chara_id, int bone) { WorldGfx_HeldItemAttach(chara_id, bone); };
    api->held_item_set = [](int item) { return static_cast<int>(WorldGfx_PlayerHeldItemSet(item)); };
    api->held_item_prev = []() {
        return static_cast<int>(WorldGfx_PlayerPrevHeldItem(&sh::game::session().system.combat));
    };
    api->event_bg_texture_fade_in = [](int file, int fade0, int fade1) { Event_BgTextureFadeIn(file, fade0, fade1); };
    api->overlay_screen_show = api->event_bg_texture_fade_in;
    api->event_bg_texture_fade_out = [](int from_black, int to_black) { Event_BgTextureFadeOut(from_black, to_black); };
    api->overlay_screen_hide = api->event_bg_texture_fade_out;
    api->player_move_speed_clear = []() {
        sh::game::session().system.characters.player.vars.harry.move_speed = 0;
    };
    api->game_warm_boot = []() { Game_WarmBoot(); };
    api->event_waypoint_set = [](int is_abs, int slot, int waypoint, int angle, int x, int z) {
        Event_PathWaypointSet(is_abs != 0, slot, waypoint, static_cast<std::int16_t>(angle), x, z);
    };
    api->event_waypoint_player = [](int anim, int count, int sub_step) {
        Event_PathWaypointExecutePlayer(anim, count, sub_step != 0);
    };
    api->chara_load = [](int model, int chara_id, int force_free) {
        return static_cast<int>(Chara_Load(model, chara_id,
                                           sh::game::session().system.characters.npc_bone_pool,
                                           static_cast<std::int8_t>(force_free), nullptr, nullptr));
    };
    api->chara_process_loads = []() { return static_cast<int>(Chara_ProcessLoads()); };
    api->event_chara_anim = [](int cmd, int slot, int anim_state, int sub_step) {
        // A slot out of range is the player's record, as the original resolved it.
        sh::entities::CharacterRecord* c = slot <= 0 ? chara(0) : chara(slot);
        Event_CharaAnimCmdExecute(cmd, c, anim_state, sub_step != 0);
    };
    api->fs_read_dms = [](int file) { return static_cast<int>(sh::loader::queue_read(file, sh::scripting::file_buffer(16))); };
    api->fs_queue_done = []() { return static_cast<int>(sh::loader::queue_load_chunks()); };
    api->dms_ready = []() { sh::scripting::cutscene_start(sh::scripting::file_buffer(16)); };
    api->pad_effect = [](int idx, int level) { Vibration_PlayAtLevel(idx, static_cast<std::uint8_t>(level)); };
    api->screen_fade_status_set = [](int status) { link::set_frame_value(link::FrameValue::ScreenFadeStatus, status); };
    api->game_state_update = []() { GameState_UnkUpdate(); };
    // The fade status "reset the timestep" (ScreenFadeState_ResetTimestep).
    api->screen_fade_reset_timestep = []() { link::set_frame_value(link::FrameValue::ScreenFadeStatus, 4); };
}

// sh.chara_*: the engine binds over the game's lazily loaded character data.
void bind_chara(ShScriptGameApi* api) {
    sh::chara::ScriptCharaDeps deps;
    deps.library = Pc_CharaData_Library();
    deps.actions_provider = []() { return Pc_CharaData_Actions(); };
    sh::chara::bind_script_api(api, deps);
}

void bind_named(ShScriptGameApi* api) {
    api->named_call = PcScriptNamed_Call;
    api->named_list = PcScriptNamed_List;
    api->raw_api_enabled = PcScriptNamed_RawApiEnabled;
}

const ShScriptGameBinderEntry g_StepperBinder(5, bind_old_steppers);
const ShScriptGameBinderEntry g_CharaBinder(20, bind_chara);
const ShScriptGameBinderEntry g_NamedBinder(60, bind_named);

}  // namespace
