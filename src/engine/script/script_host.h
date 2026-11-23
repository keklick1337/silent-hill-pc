// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's Lua script host.
// gamedata/scripts/game.lua (or the highest-priority mod's copy) runs at boot and may
// define hooks the engine calls: on_boot, on_new_game, on_map_load(map), on_update().
// A missing script is the normal case: nothing runs, nothing changes. The shipped
// gamedata/scripts/game.lua is a documented no-op, so behaviour is exactly the original's
// until a mod or the user writes a script.
//
// Sandbox: base/table/string/math only; load/loadfile/dofile/require and the os/io/package/
// debug tables are removed; every script call runs under an instruction budget (a runaway
// loop is an error, not a hang). Errors are logged once and the hook is dropped.

#pragma once

#include "engine/hooks/hook_bus.h"
#include "formats/common/status.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace sh::mods {
struct LuaLayers;
}

struct lua_State;
struct lua_Debug;

namespace sh::script {

using LogFn = std::function<void(int level, const std::string& message)>;

// 1: sh.log, sh.api_version. 2: sh.flag_get/flag_set, sh.give_item, sh.player_pos,
// sh.map_name; per-map events.lua. 3: coroutines (sh.spawn + wait/wait_frames/wait_until),
// item queries (sh.has_item/item_count/take_item), flag_set_alt, player teleport and
// freeze, map messages (sh.msg_draw), sfx/bgm, screen fades, and map-event injection
// (sh.load_map/sh.fmv) through the Event_Update contract (g_MapEventData + friends).
// 3.1: ported map handlers (sh.event_point/item_take/common_item_take, sh.state_set).
// 3.2: the message steppers (sh.event_msg/_with_sfx/_with_bg) and sh.event_paper_map_take.
// 3.3: input (sh.input_action/input_axis), camera (sh.camera_pos/look),
// sh.event_msg_with_texture, sh.flashlight, sh.background_gamma, sh.event_wait_timer.
inline constexpr int kApiVersion = 3;

// The game functions the script API calls, bound by the game layer (ShScript_SetGameApi).
// A NULL entry makes the matching sh.* function error in the script instead of crashing.
// Plain C signatures so the engine module stays game-agnostic.
/* One argument of the generic sh.call bridge (kind 0 nil, 1 integer, 2 string,
 * 3 boolean). Layout-identical to ShScriptArg in sh_script.h. */
struct CallArg {
    int kind;
    long long i;
    const char* s;
};

/* render: one sh.render.frame_hold request; layout-identical to ShRenderFrameHold in
 * sh_script.h, which documents the values. */
struct FrameHold {
    int source = 0;               // 0 previous, 1 held, 2 current
    int tint[3] = {128, 128, 128};
    int blend = 0;                // -1 none, 0 half, 1 add, 2 sub, 3 quarter
    int layer = 1;                // 0 world, 1 overlay
    int offset[2] = {0, 0};       // screen pixels
};

/* render: sh.render.world_redirect; layout-identical to ShRenderLayer/ShRenderLayers. */
struct Layer {
    int offset[2] = {0, 0};
    int tint[3] = {128, 128, 128};
    int blend = 0;                // as FrameHold
};

struct Layers {
    int count = 0;
    const Layer* items = nullptr;  // the first at the bottom
};

/* render: sh.render.tile; layout-identical to ShRenderTile. */
struct Tile {
    int x = 0, y = 0, w = 0, h = 0;  // screen pixels from the screen centre
    int colour[3] = {0, 0, 0};
    int blend = 0;                    // as FrameHold
    int layer = 3;                    // 0 world, 1 overlay, 2 picture, 3 screen
    int tpage = -1;                   // raw texture page word of the page packet; -1: computed
    int dither = 0;                   // non-zero: the page packet turns dithering on
};

/* render: one sh.render.sprite request; layout-identical to ShRenderSprites/ShRenderSpriteRect
 * in sh_script.h, which documents the values. */
struct SpriteRect {
    int x, y, w, h;               // screen pixels from the screen centre
    int u, v;                     // texels of the texture (of `tpage` when it is set)
    int tint[3] = {-1, -1, -1};   // < 0: the call's tint
    int blend = -2;               // -2: the call's blend
    int clut = -1;                // raw CLUT word, only with tpage
    int tpage = -1;               // raw draw mode; -1: the texture's page
};

struct Sprites {
    int texture = 0;              // 0 background
    int layer = 2;                // 0 world, 1 overlay, 2 picture
    int tint[3] = {128, 128, 128};
    int blend = -1;               // -1 none (opaque), 0 half, 1 add, 2 sub, 3 quarter
    int u = 0, v = 0;             // the shared uv (picks the page of an empty list)
    int count = 0;
    const SpriteRect* items = nullptr;
};

/* render: one sh.render.world_quads request; layout-identical to ShRenderWorldQuads /
 * ShRenderWorldQuad in sh_script.h, which documents the values. */
struct WorldQuad {
    int corners[4] = {0, 1, 2, 3};
    int uv[8] = {};
    int tint[3] = {128, 128, 128};
    int depth_test = -1;          // -1 average depth, 0..3 that corner's
    int screen_test = -1;         // -1 every corner, 0..3 that corner
    int precise = 0xF;            // bit k: corner k keeps its sub-pixel projection
    int sort_points[4] = {0, 1, 2, 3};
};

struct WorldQuads {
    int point_count = 0;
    const int* points = nullptr;  // x, y, z triples, world units
    int quad_count = 0;
    const WorldQuad* quads = nullptr;
    int page_x = 0, page_y = 0;
    int colours = 0;              // 0 16-colour, 1 256-colour, 2 direct
    int palette_x = 0, palette_y = 0;
    int blend = -1;               // as FrameHold; -1 opaque
    int depth_bias = 0;
    int clip_x = 1024, clip_y = 1024;
};

/* render: one sh.render.texture_load request; layout-identical to ShRenderTextureLoad. */
struct TextureLoad {
    int file = 0;     // game file number
    int texture = 0;  // 0 background, 1 paper_map
    int buffer = 0;   // 0 file (the file loader's buffer), 1 image (the image staging buffer),
                      // 2 paper_map (the paper map pictures' buffer)
};

/* render: sh.render.vram_save / vram_restore; layout-identical to ShRenderVramCopy. */
struct VramCopy {
    int x = 0, y = 0, w = 0, h = 0;  // VRAM pixels
    int buffer = 0;                   // 0 screen_backup, 1 screen_backup_alt
};

struct GameApi {
    // v2
    int (*flag_get)(int id) = nullptr;          // 0/1
    void (*flag_set)(int id, int value) = nullptr;
    void (*give_item)(int itemId, int count) = nullptr;
    void (*player_pos)(int out[3]) = nullptr;   // Q12 world position
    const char* (*map_name)() = nullptr;        // "map1_s02"; static storage
    // flag_set_alt is the "complete/disabled event flag" write the map-event code
    // uses (Savegame_EventFlagSetAlt); msg_draw returns the game's message state
    // (0 = still showing, -1 = finished, 1..3 = selection entry) and must be called
    // once per tick while a message is up; map_event_set injects a synthetic map event
    // (the same contract Event_Update leaves in g_MapEventData/g_MapEventSysState/
    // g_MapEventParam) — sysState 5 = door to mapIdx at arrival point `param`,
    // 4 = FMV `param`, 6 = same-map room warp; completeFlag is set when it is >= 0.
    void (*flag_set_alt)(int id) = nullptr;
    int (*item_count)(int itemId) = nullptr;    // 0 when not held
    void (*take_item)(int itemId, int count) = nullptr;
    void (*player_set_pos)(const int xyz[3]) = nullptr;  // Q12 world position
    void (*player_freeze)(int on) = nullptr;    // 1 freeze, 0 unfreeze
    void (*player_unfreeze_set_idle)(int setIdle) = nullptr; // exact Player_ControlUnfreeze(setIdle)
    int (*msg_draw)(int msgIdx) = nullptr;
    void (*sfx_play)(int sfxId) = nullptr;      // e_SfxId (Sfx_Base-relative raw id)
    void (*bgm_change)(int trackIdx) = nullptr;
    void (*fade_start)(int fadeIn, int isWhite) = nullptr;
    int (*fade_done)() = nullptr;               // 0/1
    void (*map_event_set)(int sysState, int param, int mapIdx, int completeFlag) = nullptr;
    // Ported map handlers (the Event_* steppers of events_util, driven once per tick):
    // event_point is the active event's pointOfInterestIdx (-1 without an event);
    // state_set is SysWork_StateSetNext — a handler ends by state_set(0) (Gameplay).
    int (*event_point)() = nullptr;
    void (*event_item_take)(int itemId, int count, int flagIdx, int msgIdx) = nullptr;
    void (*event_common_item_take)(int pickupType, int flagIdx) = nullptr;
    void (*state_set)(int sysState) = nullptr;
    // v3.2: the message steppers and the paper-map pickup. The msg variants are the
    // most-used helpers of the original handlers; they run per tick and finish the
    // event themselves. event_msg_with_sfx places the sound at the active event's
    // map point, as the shared DoorJammed/DoorLocked handlers do.
    void (*event_msg)(int hasSelection, int msgIdx, int step0, int step1, int step2,
                      int incSubStep) = nullptr;
    void (*event_msg_with_sfx)(int msgIdx, int sfxId) = nullptr;
    void (*event_msg_with_bg)(int texFileIdx, int fadeQ12_0, int fadeQ12_1, int msgIdx) = nullptr;
    void (*event_paper_map_take)(int paperMapFlagIdx, int flagIdx, int msgIdx) = nullptr;
    // v3.3: unlocks the deferred slots (clock/boiler style handlers, plaques with two
    // texts). input_action: mode 0 held / 1 clicked / 2 released / 3 pulsed, against the
    // user's controller config masks. camera_pos/look take metres (the adapter converts
    // to Q12); offsets/accels/speeds are the original Q12 args passed through.
    int (*input_action)(const char* action, int mode) = nullptr;  // "enter"/"cancel"/... -> 0/1
    int (*input_axis)(int stick, int axis) = nullptr;  // stick 0 left 1 right; axis 0 x 1 y; -128..127
    void (*camera_pos)(int warp, int posQ12[3], int argsQ12[7]) = nullptr;
    void (*camera_look)(int warp, int lookQ12[3], int argsQ12[7]) = nullptr;
    void (*event_msg_with_texture)(int texFileIdx, int fadeQ12_0, int fadeQ12_1, int msgIdx0, int msgIdx1) = nullptr;
    void (*flashlight)(int on) = nullptr;
    void (*background_gamma)(int gammaQ8) = nullptr;
    int (*event_wait_timer)(int durationQ12, int unk) = nullptr;  // 1 while waiting
    // v3.4: the multi-step handlers' own state. event_step_* read/write
    // the system record's sysStateSteps[0..2] (get/set/increment) so a Lua handler can run
    // the original step machines 1:1; the steppers below advance the steps themselves.
    int (*event_step_get)(int idx) = nullptr;
    void (*event_step_set)(int idx, int value) = nullptr;
    void (*event_step_increment)(int idx) = nullptr;
    void (*event_fade_cmd)(int cmd, int fadeOut, int fadeType, int timestepQ12, int incSubStep) = nullptr;
    void (*event_selection)(int hasSelection, int msgIdx, int step0, int step1, int step2,
                            int stepSecondary) = nullptr;
    void (*sfx_stop)(int sfxId) = nullptr;
    void (*event_waypoint_set)(int isAbs, int charaSlotIdx, int waypointIdx, int angleQ12, int xQ12,
                               int zQ12) = nullptr;
    void (*event_waypoint_player)(int animId, int waypointCount, int incSubStep) = nullptr;
    // v3.5: cutscenes. chara_load uses the NPC bone coord buffer and returns the
    // engine's success; chara_anim drives the player (slot 0) or an NPC slot via the
    // event anim stepper. fs_read_dms queues FILE_* into the engine's 16-byte buffer
    // (fs_queue_done polls it), dms_ready relocates the header. cutscene_border/
    // cutscene_flag switch the letterbox mode; cutscene_timer get/set drive the DMS
    // playback position the engine's dms update consumes.
    int (*chara_load)(int modelIdx, int charaId, int forceFree) = nullptr;   // 0/1
    int (*chara_process_loads)() = nullptr;                                  // 0/1
    void (*event_chara_anim)(int cmd, int charaSlotIdx, int animState, int incSubStep) = nullptr;
    int (*fs_read_dms)(int fileIdx) = nullptr;
    int (*fs_queue_done)() = nullptr;          // Fs_QueueChunksLoad: 1 when drained
    void (*dms_ready)() = nullptr;             // Dms_HeaderFixOffsets on the DMS buffer
    void (*cutscene_border)(int state) = nullptr;   // e_CutsceneBorderState; 20 = force show
    void (*cutscene_flag)(int on) = nullptr;        // SysFlag_CutsceneActive
    int (*cutscene_timer_get)() = nullptr;          // Q12; 0x7FFFFFFF = NO_VALUE
    void (*cutscene_timer_set)(int valueQ12) = nullptr;
    // v3.6: the deferred tail's engine surface. overlay_screen_* are the clock/
    // boiler TIM screens (the event overlay-screen set/show/hide steps); sfx_3d is the positional
    // 3D sfx one-shot; player_rotate sets the heading; the rest map 1:1.
    void (*overlay_screen_set)(int stateStep, int fileIdx, int reset) = nullptr;
    void (*overlay_screen_show)(int fileIdx, int fadeQ12_0, int fadeQ12_1) = nullptr;
    void (*overlay_screen_hide)(int fadeQ12_0, int fadeQ12_1) = nullptr;
    void (*sfx_3d)(int sfxId, int volQ8) = nullptr;  // at the active event's map point
    void (*sfx_3d_at)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8) = nullptr; // at an explicit position
    void (*pad_effect)(int idx, int level) = nullptr; // controller vibration effect insert (the event rumble helpers all use idx 9)
    void (*selection_silent)(int on) = nullptr;   // the system record's silentYesSelection preset
    void (*bg_img_transition)(int which, int alphaQ12) = nullptr; // plate-insert cross-fade
    int (*state_timer_get)(void) = nullptr;      // the system record's sysStateStepData[0] (cross-fade alpha, Q12)
    void (*state_timer_set)(int valueQ12) = nullptr;
    void (*event_step_reset)(void) = nullptr;   // SysWork_StateStepReset
    void (*camera_auto_return)(int warp) = nullptr; // vcReturnPreAutoCamWork
    void (*player_fall_tick)(int direction) = nullptr; // vy += Q12_MULT_PRECISE(dt, ±0.3)
    int (*delta_time_get)(void) = nullptr;     // g_DeltaTime (Q12 seconds)
    void (*world_object_add)(const char* name) = nullptr; // WorldGfx_ObjectAdd on g_WorldObject<N>
    void (*event_bg_texture_cmd)(int cmd, int texFileIdx, int incSubStep) = nullptr; // bg TIM puzzles
    void (*event_inv_item_cmd)(int cmd, int itemId, int itemCount, int incSubStep) = nullptr;
    int (*pickup_animate)(int itemId) = nullptr;     // Gfx_PickupItemAnimate gate
    void (*chara_anim_play_to_end)(int charaSlot, int animState) = nullptr; // slot 0 = player
    int (*chara_spawn)(int charaId, int spawnFlags, int posX, int posZ, int rotY, int stateStep) = nullptr; // returns slot
    void (*chara_spawn_flags_set)(int charaId, int spawnIdx, int spawnFlags) = nullptr;
    // v3.13: NPC slots are 1-based over the system record's npcs[] (slot 1 = npcs[0], up to 32).
    void (*chara_slot_transform_get)(int charaSlot, int outPosRotQ12[4]) = nullptr; // x, y, z, rotY
    void (*chara_slot_transform_set)(int charaSlot, const int xyzQ12[3], int rotYQ12) = nullptr;
    int (*chara_slot_chara_id_get)(int charaSlot) = nullptr; // model.charaId, -1 = empty slot
    void (*chara_slot_control_set)(int charaSlot, int controlState, int stateStep) = nullptr; // model fields
    int (*event_msg_with_audio)(int mapMsgIdx, int voiceCmd) = nullptr; // Map_MessageWithAudio; returns MapMsgState
    int (*event_activation_type_get)(void) = nullptr; // g_MapEventData->activationType, -1 outside an event
    // v3.14: PSX fixed-point math and shared event helpers. math_sin/cos are the
    // the game's sine/cosine (sh::world::fixed_sin/fixed_cos, Q12 angle -> Q12); tween_* wrap Event_TweenLinear/Reset.
    int (*math_sin)(int angleQ12) = nullptr;    // fixed_sin
    int (*math_cos)(int angleQ12) = nullptr;    // fixed_cos
    int (*event_tween_linear)(int targetQ12, int durationQ12, int timerIdx) = nullptr; // returns current Q12
    void (*event_tween_reset)(int timerIdx) = nullptr;
    void (*delta_time_set)(int valueQ12) = nullptr; // g_DeltaTime (rare per-event halving)
    void (*chara_slot_anim_flags_set)(int charaSlot, int flags) = nullptr;    // slot 0 = player
    void (*chara_slot_anim_flags_clear)(int charaSlot, int flags) = nullptr;  // Model_AnimFlags macros
    void (*water_effect_enable)(void) = nullptr;  // Water_EffectEnable
    void (*water_effect_disable)(void) = nullptr;
    void (*event_sys_state_branch_on_flag)(int flagIdx, int stepTrue, int stepFalse, int setSubStep) = nullptr;
    // v3.15: the remaining SFX variants of the map code (positions Q12, volumes Q8,
    // pitch raw s8, sfxFlags = e_SfxFlags bits) plus the held-item and reboot hooks.
    void (*sfx_flags_play)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8, int sfxFlags) = nullptr; // Sfx_WithFlagsPlay
    void (*sfx_flags_pitch_play)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8, int sfxFlags, int pitch) = nullptr;
    void (*sfx_pitch_play)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8, int pitch) = nullptr; // Sfx_WithPitchPlay
    void (*sfx_play_at_pos)(int sfxId, int xQ12, int yQ12, int zQ12, int volQ8, int pitch) = nullptr; // Sfx_WithPitchPlay
    void (*sfx_falloff_pitch_play)(int sfxId, int xQ12, int yQ12, int zQ12, int vol, int falloffQ12, int pitch) = nullptr;
    void (*held_item_show)(void) = nullptr;   // WorldGfx_HeldItemShow
    void (*held_item_hide)(void) = nullptr;   // WorldGfx_HeldItemHide
    // v3.16: per-map world-object globals (g_WorldObject0..) resolved through the
    // overlay symbol table — position Q12, rotation only on the Pose variants.
    int (*world_object_pos_get)(const char* name, int outPosQ12[3]) = nullptr; // 1 = resolved
    int (*world_object_pos_set)(const char* name, const int xyzQ12[3]) = nullptr;
    int (*world_object_rot_y_set)(const char* name, int rotYQ12) = nullptr;
    void (*held_item_attach)(int charaId, int modelBone) = nullptr; // WorldGfx_HeldItemAttach
    int (*held_item_set)(int itemId) = nullptr;  // WorldGfx_PlayerHeldItemSet
    int (*held_item_prev)(void) = nullptr;       // WorldGfx_PlayerPrevHeldItem
    void (*player_weapon_attack_set)(int weaponAttack) = nullptr; // packed WEAPON_ATTACK
    int (*player_weapon_attack_get)(void) = nullptr;
    int (*math_atan2)(int yQ12, int xQ12) = nullptr; // ratan2 (bit-exact)
    void (*event_bg_texture_fade_in)(int texFileIdx, int fadeTimestep0Q12, int fadeTimestep1Q12) = nullptr;
    void (*event_bg_texture_fade_out)(int fadeTimestepFromBlackQ12, int fadeTimestepToBlackQ12) = nullptr;
    // v3.17: the remaining co-blockers of the v3.16 port audit.
    void (*sd_call)(int cmd) = nullptr;                        // SD_Call (raw sound command)
    void (*sfx_attributes_update)(int sfxId, int balance, int vol, int pitch) = nullptr; // Sd_SfxAttributesUpdate
    int (*screen_fade_finished)(void) = nullptr;               // ScreenFade_IsFinished
    int (*world_object_rot_y_get)(const char* name) = nullptr; // q3_12 rotY of a g_WorldObject<N>
    void (*player_move_speed_clear)(void) = nullptr; // Player_MoveSpeedClear (moveSpeed = NO_VALUE)
    // v3.18: the game's own RNG (Rng_Rand16 LCG) — Rng_GenerateInt/UInt 1:1, so
    // scripted rolls advance the stream exactly like the C++ handlers did.
    int (*rng_int)(int low, int high) = nullptr;   // Rng_GenerateInt
    int (*rng_uint)(int low, int high) = nullptr;  // Rng_GenerateUInt
    void (*game_warm_boot)(void) = nullptr;   // Game_WarmBoot
    int (*screen_fade_timestep_get)(void) = nullptr;  // g_ScreenFadeTimestep (Q12)
    void (*screen_fade_timestep_set)(int valueQ12) = nullptr;
    void (*player_rotate)(int angleQ12) = nullptr;   // heading (Q12 of degrees)
    int (*player_rot_y_get)(void) = nullptr;       // player heading (Q12 of degrees)
    int (*event_wait_player_stop)() = nullptr;       // 1 while waiting (advances steps itself)
    void (*event_step_increment_delayed)(int delayQ12, int reset) = nullptr;
    void (*event_step_increment_after_fade)(int stateStep, int cond, int fadeType,
                                            int timestepQ12, int reset) = nullptr;
    void (*screen_fade_status_set)(int status) = nullptr;   // raw g_Screen_FadeStatus write
    void (*game_state_update)() = nullptr;                  // GameState_UnkUpdate
    void (*delta_time_reset)() = nullptr;                   // g_DeltaTime = 0
    void (*screen_fade_reset_timestep)() = nullptr;
    // v3.7: the last two gaps. ambient_sfx polls the ambient VAB init (1 when
    // running); player_vel_y get/set expose the vertical velocity the cutscene
    // fall physics writes (vy += dt * k), in Q12 per tick.
    int (*ambient_sfx)() = nullptr;                 // Sd_AmbientSfxInit -> 0/1
    int (*player_vel_y_get)() = nullptr;            // Q12
    void (*player_vel_y_set)(int vyQ12) = nullptr;
    // Character data (gamedata/chara). Every lookup takes the fallback the
    // caller would use without data; chara_param reports through *found whether it was set.
    int (*chara_id_find)(const char* name) = nullptr;  // "Chara_Cheryl"/"Cheryl"/"28" -> id, -1
    int (*chara_sfx)(int charaId, const char* slot, int fallback) = nullptr;
    double (*chara_param)(int charaId, const char* name, double fallback, int* found) = nullptr;
    int (*chara_param_q12)(int charaId, const char* name, int fallbackQ12) = nullptr;
    int (*chara_anim)(int charaId, const char* name, int fallback) = nullptr;
    // A cutscene state by name (Harry's "states", an NPC's "controls"), -1 when unknown.
    int (*chara_state_number)(int charaId, const char* name) = nullptr;
    // Items by name: "HealthDrink", "silenthill:HealthDrink",
    // "mymod:Crowbar" -> the engine's number, -1 when unknown. Every sh.* item argument takes
    // a name; a number still works (old scripts).
    int (*item_number)(const char* name) = nullptr;
    // The common pickup of an item name (FirstAidKit ... ShotgunShells) -> e_CommonPickupItemId, -1.
    int (*common_pickup_number)(const char* name) = nullptr;
    // Event flags by name (gamedata/flags/*.json): "M1S03_ShotgunShells0",
    // "mymod:door_opened" -> the save bit, -1 when unknown. Every sh.* flag argument takes a name.
    int (*flag_number)(const char* name) = nullptr;
    // Game files by name ("TIM/LOCKER1.TIM") -> the FILE_* number, -1 when the disc has none.
    int (*file_number)(const char* name) = nullptr;
    int (*paper_map_number)(const char* name) = nullptr;
    int (*sd_command_number)(const char* name) = nullptr;
    // sh.text(key) and message keys (sh.msg_draw("map0_s01.whats_that"), say(key)).
    const char* (*text_get)(const char* key) = nullptr;  // NULL: no such key
    int (*msg_index)(const char* key) = nullptr;          // -1: the current map has no such message
    // The raw bridge (script_raw_api). One entry serves
    // sh.call(fn, ...) and the named sh.ovl_get/chara_get/sys_get/vec_get/dms_* binds: `name`
    // is the bind or function name; returns the number of values written to `out` (<= outCap),
    // or -1 with a message in `err`. Handle strings ("@SYM", "chara:1", "buf:11", ...) are
    // resolved by the game side.
    int (*evb_call)(const char* name, int argc, const CallArg* argv, long long* out, int outCap,
                    char* err, int errCap) = nullptr;
    // The flat functions of the maps 0-3 handlers (sh_script.h has the per-field notes).
    int (*fs_read)(int fileIdx, int bufIdx) = nullptr;
    void (*fs_wait)() = nullptr;
    int (*fs_seek)(int fileIdx) = nullptr;
    void (*dms_fix)(int bufIdx) = nullptr;
    void (*dms_chara_apply)(int charaSlot, const char* name, int timeQ12, int bufIdx) = nullptr;
    void (*dms_camera_apply)(int timeQ12, int bufIdx) = nullptr;
    void (*vc_cam_target)(const int xyzQ12[3], int warp) = nullptr;
    void (*vc_watch_target)(const int xyzQ12[3], int warp) = nullptr;
    void (*vc_view_position)(int outQ12[3]) = nullptr;
    void (*light_intensity_set)(int valueQ12) = nullptr;
    void (*light_bones_clear)() = nullptr;
    void (*light_pos_set)(const int xyzQ12[3]) = nullptr;
    void (*light_rot_set)(const int xyzQ12[3]) = nullptr;
    void (*flashlight_attrs_fix)() = nullptr;
    int (*math_vec2_mag_q6)(int xQ12, int zQ12) = nullptr;
    void (*chara_model_clear)(int charaSlot) = nullptr;
    void (*chara_props_reset)(int charaSlot) = nullptr;
    void (*player_weapon_stash)() = nullptr;
    void (*player_weapon_restore)() = nullptr;
    void (*map_effects_init)(int idx0, int idx1) = nullptr;
    int (*sys_flags_get)() = nullptr;
    void (*sys_flags_set)(int value) = nullptr;
    int (*bgm_status_flags_get)() = nullptr;
    void (*bgm_status_flags_set)(int value) = nullptr;
    int (*sys_timer_get)() = nullptr;
    void (*sys_timer_set)(int valueQ12) = nullptr;
    int (*event_msg_audio)(int alias, int mapMsgIdx, const char* idxSym, int audioIdx, const char* cmdsSym) = nullptr;
    void (*event_msg_dimmed_bg)(int texFileIdx, int fade0, int fade1, int msgIdx0, int msgIdx1) = nullptr;
    void (*chara_anim_unlock_play_to_end)(int charaSlot) = nullptr;
    void (*paper_map_state)(int state, int paperMapIdx) = nullptr;
    void (*paper_map_cmd)(int cmd, int paperMapIdx) = nullptr;
    void (*map_screen_vertex_draw)(int a0, int a1, int a2) = nullptr;
    int (*map_screen_marking_draw)(const int args[7]) = nullptr;
    int (*map_screen_player_mark_draw)(int a0, int a1, int a2, int a3) = nullptr;
    void (*map_box_outline_draw)(const int args[9]) = nullptr;
    int (*sd_play_sfx)(int sfxId, int balance, int vol) = nullptr;
    int (*vc_stereo_balance)(const int xyzQ12[3]) = nullptr;
    int (*light_intensity_get)() = nullptr;
    int (*chara_get)(int charaSlot, int field, int* ok) = nullptr;
    int (*chara_set)(int charaSlot, int field, int value) = nullptr;
    int (*chara_anim_start_keyframe_get)(int charaSlot) = nullptr;
    void (*math_point_from_angle)(const int xyzQ12[3], int angleQ12, int arg2, int arg3) = nullptr;
    void (*chara_bones_init)(int idx) = nullptr;
    void (*camera_default_speeds)(int out[4]) = nullptr;
    int (*fs_entry_loaded)(int queueIdx) = nullptr;
    void (*xa_preload)(int xaIdx) = nullptr;
    int (*map_effects_update)(int idx0, int idx1, int primType, const char* primDataSym, int arg4, int arg5) = nullptr;
    int (*savegame_get)(int field) = nullptr;
    void (*savegame_set)(int field, int value) = nullptr;
    void (*cursor_draw)(const int args[12]) = nullptr;
    int (*mouse_cursor_puzzle_override)(const char* xSym, const char* ySym) = nullptr;
    void (*game_timer_update)() = nullptr;
    int (*item_remove)(int itemId, int count) = nullptr;
    // The named engine API sh.<group>.<name>(...): named_call has the
    // evb_call contract; named_list is every "<group>.<name>" (NULL-terminated) the host puts
    // into the sh.<group> tables (none without it).
    // raw_api_enabled (the script_raw_api config key) also exposes the raw bridge
    // (sh.call, ovl_*/chara_*/sys_*/vec_*/dms_*); NULL or 0 = hidden.
    int (*named_call)(const char* name, int argc, const CallArg* argv, long long* out, int outCap,
                      char* err, int errCap) = nullptr;
    const char* const* (*named_list)() = nullptr;
    int (*raw_api_enabled)() = nullptr;
    /* render */
    // sh.render.frame_hold{...}: 0 drawn, -1 refused with a message in err.
    int (*render_frame_hold)(const FrameHold* spec, char* err, int errCap) = nullptr;
    // sh.render.sprite{...}: 0 drawn, -1 refused with a message in err.
    int (*render_sprites)(const Sprites* spec, char* err, int errCap) = nullptr;
    // sh.render.texture_load{...}: 0 queued, -1 refused with a message in err.
    int (*render_texture_load)(const TextureLoad* spec, char* err, int errCap) = nullptr;
    // sh.render.held_clear() / held_capture(): 0 done, -1 refused with a message in err.
    int (*render_held_clear)(char* err, int errCap) = nullptr;
    int (*render_held_capture)(char* err, int errCap) = nullptr;
    // sh.render.world_redirect{...}: 0 drawn, -1 refused with a message in err.
    int (*render_world_redirect)(const Layers* spec, char* err, int errCap) = nullptr;
    // sh.render.tile{...}: 0 drawn, -1 refused with a message in err.
    int (*render_tile)(const Tile* spec, char* err, int errCap) = nullptr;
    // sh.render.fade_in_level(): 4096 = the screen fully faded in.
    int (*render_fade_in_level)() = nullptr;
    // sh.render.vram_save{...} / vram_restore{...}: 0 done, -1 refused with a message in err.
    int (*render_vram_save)(const VramCopy* spec, char* err, int errCap) = nullptr;
    int (*render_vram_restore)(const VramCopy* spec, char* err, int errCap) = nullptr;
    // sh.render.display_init{width=, interlaced=}: 0 done, -1 refused.
    int (*render_display_init)(int width, int interlaced, char* err, int errCap) = nullptr;
    // sh.render.display_swap() / vblank_interval(n) / picture_screen(on).
    void (*render_display_swap)() = nullptr;
    void (*render_vblank_interval)(int vblanks) = nullptr;
    void (*render_picture_screen)(int on) = nullptr;
    // sh.render.background{texture=}: 0 drawn, -1 refused.
    int (*render_background)(int texture, char* err, int errCap) = nullptr;
    // sh.paper_map_grant(name) / sh.chara_spawn_entry_set(entry, on).
    void (*paper_map_grant)(int paperMapIdx) = nullptr;
    void (*chara_spawn_entry_set)(int entry, int on) = nullptr;
    // sh.render.world_quads{...}: the number of quads drawn, -1 refused with a message in err.
    int (*render_world_quads)(const WorldQuads* spec, char* err, int errCap) = nullptr;
};

// Instruction budget for one hook/script call. A normal hook body is a few hundred; the
// limit exists to turn an accidental infinite loop into a logged error.
inline constexpr std::uint32_t kInstructionBudget = 5'000'000;

struct HostConfig {
    std::filesystem::path gamedata_dir;  // scripts at <gamedata_dir>/scripts/game.lua
    std::filesystem::path mods_dir;      // mods' scripts/main.lua run after the base script (load())
    GameApi game;                        // the sh.* game functions; NULL entries error
    LogFn log;                           // NULL: stderr
    double updates_per_second = 30.0;    // the game's logic rate: wait(sec) counts these
};

// The configuration the game's own scripts run with (sh_script.cpp: the bound sh.* game API,
// the data directories of ShScript_Boot, the game log), for other hosts of the running game
// such as the character action scripts.
HostConfig game_host_config();

class Host;

// `mods reload` of a game host: closes it (its hook handlers, console commands and coroutines go with it), then a
// new host with `config` loads the game script and every mod's scripts/main.lua again and runs on_boot. Not from
// inside one of the host's own scripts. On a load error `host` is left empty.
Result<bool> reload_host(std::unique_ptr<Host>& host, HostConfig config);

class Host {
public:
    // A script that spawns in a loop without waiting would otherwise grow the list
    // forever; the limit turns it into a script error long before memory matters.
    static constexpr std::size_t kMaxCoroutines = 256;

    explicit Host(HostConfig config);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // Runs gamedata/scripts/game.lua, then every active mod's scripts/main.lua in load order
    // (only mods whose mod.json says "scripts": true), each mod in its own environment with
    // the global `mod` = {id, name, version, dir, storage}. false: no script anywhere (the
    // normal case). An error in the base script fails with the Lua message (the host then
    // has no script and every hook is a no-op); an error in a mod's main.lua is logged and
    // only that mod is left out.
    Result<bool> load();
    // The mods whose main.lua ran, in load order.
    std::vector<std::string> loaded_mods() const;

    // The same for one explicit file (a per-map events.lua). false when it does not exist.
    Result<bool> load_file(const std::filesystem::path& script);

    // A layered file (`relative` is a gamedata path: "maps/map0_s01/events.lua", "chara/Groaner/behavior.lua"):
    // the base file from config().gamedata_dir runs in this state's globals (or the copy of the mod whose
    // mod.json "replace" lists the path), then every other active mod's copy, in load order, runs as an
    // extension: its own environment whose reads fall back to the globals, with `mod` and the global
    // extend(name, function(base, ...) ... end). Creates the state when there is none. false: no layer has
    // the file. An error in the base fails like run_file; an error in an extension is logged and leaves out
    // only that extension.
    Result<bool> run_layered(std::string_view relative);
    Result<bool> run_layers(const mods::LuaLayers& layers);
    std::vector<std::string> extension_mods() const;  // the mods whose extensions ran, in load order

    // sh.hooks of this state: the handlers its scripts registered on the engine bus are
    // tagged with hook_group() and leave the bus when the state closes.
    const std::string& hook_group() const { return hook_group_; }
    std::size_t hook_count() const { return lua_hooks_.size(); }
    // Bindings of sh.hooks (bind_hooks.cpp). The function to register is on the top of `L`'s stack (popped).
    hooks::Token hook_on(lua_State* L, const std::string& owner, std::string_view event, int priority,
                         const std::string& id);
    bool hook_off(hooks::Token token);
    bool hook_off(const std::string& owner, std::string_view event, std::string_view id);
    // The owner of what the state's globals register: "silenthill", or the mod whose copy replaced the base.
    const std::string& base_owner() const { return base_owner_; }
    // A warning given once per key for the life of the state.
    void warn_once(const std::string& key, const std::string& message);
    // lua_pcall of the function and `nargs` arguments on the stack under the instruction budget.
    int pcall_budget(int nargs, int nresults);

    // The hooks the script may define. Missing hooks are fine; a hook that errors is
    // logged and dropped. Each mod's hook of the same name runs after the base one, in load
    // order; a mod hook that errors is logged once with the mod id and not called again. on_update() is called every frame and returns immediately
    // when the script does not define it; it also pumps the script's coroutines
    // (sh.spawn): those started by any hook or by another coroutine run one wait step
    // per update — wait(sec) counts real update ticks, wait_until(fn) polls fn each tick.
    void on_boot();
    void on_new_game();
    void on_map_load(std::string_view map);
    void on_update();

    // Coroutines alive right now (sh.spawn minus finished/errored ones).
    std::size_t coroutine_count() const { return coroutines_.size(); }
    double updates_per_second() const { return config_.updates_per_second; }

    // Registers a coroutine thread (already carrying its pending function call on the
    // stack) ref'd in the Lua registry; it runs on the next on_update pump.
    void add_coroutine(int thread_ref, int nargs);

    // Map handlers: a global function of the handler's name (from handlers.json) called once
    // per tick. has_global is a cheap existence check the dispatcher uses before every call.
    bool has_global(std::string_view name);
    bool call_global(std::string_view name);

    // Character action hooks: global `name` called with one table argument
    // {key = value, ...}; its first result as an integer (kind 1) or a string (kind 2). A
    // missing function, another result or an error (logged) gives kind 0.
    struct HookField {
        const char* key;
        CallArg value;
    };
    struct HookResult {
        int kind = 0;
        long long integer = 0;
        std::string text;
    };
    HookResult call_global_table(std::string_view name, std::span<const HookField> fields);
    /* A character behaviour (gamedata/chara/<Name>/behavior.lua) is a shared
     * prelude plus the type's own file in one state. run_file runs one more file in the
     * state load_file made (or makes the state, like load_file, when there is none yet);
     * call_global_ints calls a global with integer arguments (update(slot)). */
    Result<bool> run_file(const std::filesystem::path& script);
    bool call_global_ints(std::string_view name, std::initializer_list<long long> args);
    /* The same with an integer result, callable from inside a call into this state (an engine
     * callback the script's own call triggers): the outer call's instruction budget is kept. */
    bool call_global_int(std::string_view name, std::initializer_list<long long> args, long long* result);

    /* console: calls the function stored under registry ref `ref` (luaL_ref) under the instruction budget.
     * push adds the arguments and returns their count; take sees the one result on the stack top. An error is
     * logged, copied to `error` (when given) and gives false. Works from inside a call into this state. */
    bool call_ref(int ref, const std::function<int(lua_State*)>& push, const std::function<void(lua_State*)>& take,
                  std::string* error);

    /* The modding tools' Lua line: runs `code` in this state's globals under the
     * instruction budget, as an expression first ("return <code>") and as a statement when that does not
     * compile. `out` gets what print() wrote during the run and the results (tostring, tab-separated), or the
     * error. False on a compile or run error. Never called by the game itself. */
    bool run_snippet(std::string_view code, std::string* out);
    /* The modding tools' test runner (docs/modding/scripting.md, "Tests"): compiles `code` as `chunk_name`, runs
     * `harness` (which returns the environment table for the chunk and a function that runs it), sets that table
     * as the chunk's _ENV and calls the function with the chunk under the instruction budget. `out` gets the
     * printed text and the function's result, or "SHTEST ERROR\t<message>". False on a compile or run error. */
    bool run_test_file(std::string_view harness, std::string_view code, const std::string& chunk_name,
                       std::string* out);

    bool loaded() const { return L_ != nullptr; }
    const std::string& script_path() const { return script_path_; }

    // The sh.log API function reports through this.
    void log(int level, const std::string& message) const;

    void set_game_api(const GameApi& game) { config_.game = game; }
    const GameApi& game_api() const { return config_.game; }
    const HostConfig& config() const { return config_; }

private:
    struct CallScope;  // installs the instruction budget around one pcall

    // A script coroutine between resumes. Both refs live in the Lua registry
    // (lua_ref); kNoRef (-1, Lua's LUA_NOREF) means "not set".
    static constexpr int kNoRef = -1;
    struct Coroutine {
        int thread_ref = kNoRef;      // registry ref to the thread
        int pred_ref = kNoRef;        // wait_until(fn): registry ref to the predicate
        int frames_left = 0;          // wait(sec)/wait_frames(n)
        bool started = false;         // resumed at least once (sh.spawn defers to the pump)
        int nargs = 0;                // arguments still on the thread's stack
    };

    // A mod's scripts/main.lua: its environment table (registry ref) holds its globals and hooks.
    // An extension layer (run_layered) is the same: its lifecycle hooks (on_boot, on_update...) run after the
    // base's, and the functions it defines are what has_global / call_global* find first.
    struct ModScript {
        std::string id;
        int env_ref = kNoRef;
        std::vector<std::string> broken;  // hooks that errored once and are no longer called
        bool extension = false;
    };

    // One sh.hooks.on handler of this state.
    struct LuaHook {
        hooks::Token token = 0;
        std::string event;
        std::string owner;
        std::string id;
        int ref = kNoRef;  // the Lua function in the registry
    };
    void dispatch_hook(const std::shared_ptr<LuaHook>& hook, hooks::Event& event);
    void close_state();              // drops the state's handlers from the bus, then the state

    bool create_state();             // a fresh sandboxed state; false when Lua cannot allocate one
    // Leaves the new environment on the stack (true), or logs and leaves nothing.
    bool push_mod_environment(const std::string& id, const std::string& name, const std::string& version,
                              const std::filesystem::path& root, bool extension, const std::string& where);
    void load_mod_script(const std::string& id, const std::string& name, const std::string& version,
                         const std::filesystem::path& root);
    // Pushes the function `name` as the scripts see it: the last extension defining it, else the global.
    bool push_function(const char* name);
    template <typename... Push>
    void call_mod_hooks(const char* name, Push&&... push);
    void open_sandbox();
    void open_coroutine_api();       // sh.spawn + wait/wait_frames/wait_until + prelude
    void pump_coroutines();
    bool resume_one(Coroutine& co, const char* where);  // false: finished or errored (logged)
    void drop_coroutine(std::size_t i, const char* why);
    // Calls global `name` with `args`; returns false when the call errored (already logged).
    template <typename... Push>
    bool call_hook(const char* name, Push&&... push);
    // The error of a per-tick call (map handler, behaviour update): a repeat of the last one is counted, not
    // logged again, so a script failing every frame does not flood the log.
    void log_call_error(const std::string& message);

public:
    static void count_hook(lua_State* L, lua_Debug* ar);

private:

    HostConfig config_;
    lua_State* L_ = nullptr;
    std::string script_path_;
    bool update_ok_ = true;  // on_update survives its first error
    std::vector<Coroutine> coroutines_;
    int coroutine_serial_ = 0;  // sh.spawn's returned id
    std::vector<ModScript> mods_;
    std::string base_owner_ = "silenthill";
    std::string hook_group_;
    std::shared_ptr<bool> alive_;  // the bus's handlers hold it weakly: a closed host is never called
    std::map<hooks::Token, std::shared_ptr<LuaHook>> lua_hooks_;
    std::set<std::string> warned_;
    std::string last_call_error_;
    std::uint64_t call_error_repeats_ = 0;
};

// sh.hooks (owner = the mod id, or "" for the state's own globals) and the sh.mods.get / mod.export pieces
// (bind_hooks.cpp). Each pushes onto the stack.
void push_hooks_api(lua_State* L, Host* host, const std::string& owner);
void push_mods_get(lua_State* L, Host* host);
// The registry table of mod.export()ed APIs of a state (created on first use).
void push_exports(lua_State* L);

}  // namespace sh::script
