// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game operations the named script API (named_api.cpp) forwards to other modules: event steppers, character
// loads, file queue, cutscene tracks, camera targets, world draw switches and map effects. Each keeps its C name;
// this is the narrow C ABI view of those functions, with the engine's types: Q12 numbers and enum values are 32-bit
// integers, the C code's `bool` is its 4-byte boolean (CBool), pointers are opaque.

#pragma once

#include "engine/ids/game_numbers.h"
#include <cstdint>

namespace sh::script::named_calls {
using CBool = std::int32_t;
}

extern "C" {

// Event steppers.
void Event_ScreenFadeCmd(std::int32_t cmd, sh::script::named_calls::CBool fade_out, std::int32_t fade_type,
                         std::int32_t timestep, sh::script::named_calls::CBool sub_step);
void Event_WaitTimer(std::int32_t delay, sh::script::named_calls::CBool sub_step);
void Event_WaitPlayerStop(void);
void Event_SysStateBranchOnFlag(std::int32_t flag, std::int32_t step_true, std::int32_t step_false,
                                sh::script::named_calls::CBool sub_step);
void Event_DisplayMapMsg(sh::script::named_calls::CBool selection, std::int32_t message, std::int32_t step0,
                         std::int32_t step1, std::int32_t step2, sh::script::named_calls::CBool sub_step);
void Event_BgTextureCmd(std::int32_t cmd, std::int32_t file, sh::script::named_calls::CBool sub_step);
void Event_BgTextureFadeOut(std::int32_t from_black, std::int32_t to_black);
void Event_BgTextureLoadFadeIn(std::int32_t file, std::int32_t timestep);
void Event_InvItemCmd(std::int32_t step, std::int32_t item, std::int32_t count, sh::script::named_calls::CBool sub_step);
void Event_DisplayMapMsgWithAudio(std::int32_t message, std::uint8_t* voice_index, const std::uint16_t* voice_cmds);
void Map_MessageWithAudio(std::int32_t message, std::uint8_t* voice_index, const std::uint16_t* voice_cmds);
void Event_DisplayMapMsgWithDimmedBg(std::int32_t file, std::int32_t timestep0, std::int32_t timestep1,
                                     std::int32_t message0, std::int32_t message1);
void Event_DisplayMapMsgWithSfx(std::int32_t message, std::int32_t sfx, void* sfx_position);
void Event_CameraPositionSet(void* base, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t accel_xz,
                             std::int32_t accel_y, std::int32_t speed_xz, std::int32_t speed_y,
                             sh::script::named_calls::CBool warp);
void Event_CameraLookAtSet(void* base, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t accel_x,
                           std::int32_t accel_y, std::int32_t speed_x, std::int32_t speed_y,
                           sh::script::named_calls::CBool warp);
std::int32_t Event_TweenLinear(std::int32_t target, std::int32_t duration, std::int32_t timer);
void Event_TweenReset(std::int32_t timer);
void Event_RampReset(std::int32_t timer);
std::int32_t Event_RampAdvance(std::int32_t target, std::int32_t duration, std::int32_t timer);
void Event_PathWaypointSet(sh::script::named_calls::CBool absolute, std::int32_t slot, std::int32_t waypoint,
                           std::int16_t heading, std::int32_t x, std::int32_t z);
void Event_PathWaypointExecutePlayer(std::int32_t anim, std::int32_t waypoints, sh::script::named_calls::CBool sub_step);
void Event_CharaAnimCmdExecute(std::int32_t cmd, void* chara, std::int32_t state, sh::script::named_calls::CBool sub_step);
void Event_CharaAnimPlayToEnd(void* chara, std::int32_t state);
void Event_CharaAnimUnlockPlayToEnd(void* chara);

// Messages and pickups (text/message_box_bridge.cpp, items/).
std::int32_t Gfx_MapMsg_Draw(std::int32_t message);
sh::script::named_calls::CBool Gfx_PickupItemAnimate(int item);
sh::script::named_calls::CBool Player_ItemRemove(int item, int count);

// Characters (events/npc_spawn_bridge.cpp, gfx/chara_vram_bridge.cpp, game_boot/).
std::int32_t Chara_Spawn(std::int32_t chara, std::int32_t flags, std::int32_t x, std::int32_t z, std::int16_t yaw,
                         std::uint32_t state_step);
sh::script::named_calls::CBool Chara_Load(std::int32_t model, std::int32_t chara, void* bones, std::int8_t force_free,
                                          void* model_buffer, void* texture);
sh::script::named_calls::CBool Chara_ProcessLoads(void);
void Chara_ModelCharaIdClear(void* chara, std::int32_t unused0, std::int32_t unused1);
void Chara_FsImageCalc(void* image, std::int32_t groups, std::int32_t model);
void Anim_CharaTypeAnimInfoClear(void);

// World draw switches (gfx/world_draw.cpp, gfx/held_item_bridge.cpp).
void WorldGfx_HeldItemShow(void);
void WorldGfx_HeldItemHide(void);
std::int32_t WorldGfx_PlayerHeldItemSet(std::int32_t item);
std::int32_t WorldGfx_PlayerPrevHeldItem(void* combat);
void WorldGfx_ObjectAdd(void* model, const void* position, const void* rotation);
void WorldObject_ModelNameSet(void* model, char* name);
void Ipd_CloseRangeChunksInit(void);

// Files.
void GameFs_PlayerMapAnimLoad(std::int32_t map);
void GameFs_StfRollBinLoad(void);

// Cutscene tracks (engine/scripting/cutscene_calls.cpp).
int PcCutscene_Load(const char* path, void* slot);
int PcCutscene_FrameCount(void* slot);
void PcCutscene_CharacterApply(void* chara, const char* track, int time, void* slot);

// Camera targets (view/camera_bridge.cpp, view/camera_driver_bridge.cpp).
void vcChangeProjectionValue(std::int16_t screen_y);
void vcUserCamTarget(void* position, void* params, sh::script::named_calls::CBool warp);
void vcUserWatchTarget(void* position, void* params, sh::script::named_calls::CBool warp);
void vcReturnPreAutoCamWork(sh::script::named_calls::CBool warp);
void Vc_UpdateLookAtPointSetAlt(void);

// Light, world and map effects (gfx/).
void Game_FlashlightAttributesFix(void);
void Game_TurnFlashlightOn(void);
void Game_TurnFlashlightOff(void);
void Pc_FogFloorRequest(void);
void Water_EffectEnable(void);
void Water_EffectDisable(void);
void Gfx_MapInitMapEffectsUpdate(std::int32_t index0, std::int32_t index1);
void Map_EffectStatesReset(void);
void Math_PointFromAngleCalc(void* at, std::int16_t size, std::int32_t color, std::int32_t delay);
void Map_WeatherEnvironmentSet(std::int8_t environment, std::uint32_t light_request);
void Map_WeatherUpdate(std::int32_t unused, std::int32_t map, std::int32_t phase);
sh::script::named_calls::CBool Map_WeatherDensitySet(std::int32_t level);
void SlotFx_LightGlowSpawn(void* at, std::uint16_t size, std::uint16_t spread, std::uint32_t flags);
void SlotFx_EmitterStart(void);
void SlotFx_SmokeStart(void);
void SlotFx_DripsStart(std::int32_t count);
void DrainSplash_Start(const void* at);
void DrainSplash_Update(void);
void UfoEnding_SpriteDraw(std::int32_t index);
void UfoEnding_SlideInDraw(std::int16_t progress);
void GroundMorph_Draw(std::int32_t level);
void Glass_Shatter(void* from, void* to, std::int16_t speed, std::int32_t size, std::int16_t half_a,
                               std::int16_t half_b, std::int32_t spread, std::int32_t group);
void WaterStream_Start(void);
void WaterStream_Stop(void);

// Game state (items/item_screen_flow_bridge.cpp, events/game_sys_states.cpp, pc/state/screens_view.cpp, collision/).
void Game_TimerUpdate(void);
void GameState_UnkUpdate(void);
void Screen_BackgroundImgDraw(void* image);
void Screen_BackgroundImgDrawAlt(void* image);
void Vibration_Play(std::int32_t effect);
void Vibration_PlayAtLevel(std::int32_t effect, std::uint8_t level);
void Collision_SurfaceGet(void* surface, std::int32_t x, std::int32_t z);
void Collision_FlagBitsSet(std::uint16_t bits);
void Collision_FlagBitsClear(std::int32_t bits);
sh::script::named_calls::CBool Vw_AabbVisibleInScreenCheck(std::int32_t min_x, std::int32_t max_x, std::int32_t min_y,
                                                           std::int32_t max_y, std::int32_t min_z, std::int32_t max_z);
void Player_FallBackward(void);

// Names of the data (pc/pc_legacy_ids.cpp).

}  // extern "C"
