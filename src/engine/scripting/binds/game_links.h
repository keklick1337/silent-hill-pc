// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// What the named script binds (src/engine/scripting/binds) take from other modules, in one
// place: the extern "C" functions the binds call as they are (plain numbers, names and
// opaque addresses only). The accessors over records are in record_links.h.

#pragma once

#include "engine/loader/active_map.h"
#include "engine/ids/game_numbers.h"
#include <cstdint>

#include "engine/scripting/binds/record_links.h"

extern "C" {
// Music.
void Bgm_Update(std::int32_t layer_flags, std::int32_t fade_speed, std::uint8_t* layer_limits);
std::uint32_t Bgm_CurrentBgmIdxGet(void);  // the music pitch wobble, advanced
void Bgm_ChannelSet(void);
std::uint16_t Sd_UnkU16Get(void);  // busy music channels of the sound driver
void Sd_BgmLayerVolumeSet(std::uint8_t layer, std::uint8_t volume);
void Sd_SfxAttributesUpdate(std::uint16_t sfx, std::int8_t balance, std::uint8_t volume, std::int8_t pitch);
void Pc_CarouselMix_Reset(void);
int Pc_CarouselMix_Update(int balance, int base_volume, int distance, int range, int mode, int delta_time,
                          int* out_pan, int* out_volume);

// Camera.
std::int32_t vcRetCamMvSmoothF(void);  // 0 = the camera chases the player
std::int32_t vcExecCamera(void);
std::int32_t Vc_LookAtPositionYGet(void);
std::int32_t Vc_VectorMagnitudeCalc(std::int32_t x, std::int32_t y, std::int32_t z);
std::int32_t vwRetNewAngSpdToTargetAng(std::int32_t speed, std::int16_t angle, std::int16_t target,
                                       std::int32_t accel, std::int32_t max_speed, std::int32_t decel);

// World and effects.
void Gfx_MapEffectsUpdate(std::int32_t preset0, std::int32_t preset1, int value_type, void* value,
                          std::int32_t min, std::int32_t max);
std::uint8_t WorldEnv_DarknessAtGet(std::int32_t x, std::int32_t y, std::int32_t z);
void Combat_VectorUnkSet(std::int32_t* position, std::int8_t kind, std::int16_t value);  // a combat table point
int LakeSurface_TexturesInitIfDeclared(int rebuild);
void* PcScriptNamed_RefAddr(const char* ref, char* err, int err_cap);
void SlotFx_SmokeStart(void);
void* MapTables_DataSymbol(const char* name);
const char* PcPort_GetGameDataPath(void);

// The final bosses' effects.
int EndingFx_Enabled(void);
void EndingFx_FireCue(const std::int32_t* record);
void EndingFx_ThunderGapsReset(void);
void EndingFx_ThunderGapsTick(void);
void EndingFx_LightningInit(void* position);
void EndingFx_LightningClear(void);
void EndingFx_LightningUpdate(void);
void EndingFx_LightningBolt(std::int32_t unused);
void EndingFx_LightningVolumeSet(std::int32_t volume);
void EndingFx_LightningStop(void);
void EndingFx_LightningResume(void);
void EndingFx_FlareDraw(std::int32_t origin);
void EndingFx_FireInit(void);
void EndingFx_FireStop(void);
void EndingFx_FireResume(void);
void EndingFx_FireUpdate(void);
void EndingFx_BoltsLoad(void);
void EndingFx_BeamsInit(void);
void EndingFx_BeamsModeSet(std::int32_t mode);
void EndingFx_BeamsUpdate(void);

// Characters and maps.
void Chara_SpawnPositionSet(int chara, int spawn, int x, int z);
void MapData_SpawnVariantsApply(void);
void Player_DamageFeetFront(void);
}
