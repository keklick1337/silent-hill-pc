/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Vladislav Tislenko (keklick1337) */
#ifndef ENGINE_CUTSCENE_CUTSCENE_C_H
#define ENGINE_CUTSCENE_CUTSCENE_C_H

/* C facade of the cutscene player (engine/cutscene) over the VFS: a cutscene is
 * loaded from convert/cutscenes/<dir>/<NAME>.dms.json, a mod's copy first. Slots are opaque keys the caller
 * chooses; times are Q12 frames (4096 = one frame at 30 fps), positions Q12 world units, angles Q12 turns.
 * Implemented in cutscene_c.cpp (target sh_cutscene_c). */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ShCutsceneActorPose
{
    int32_t position[3];
    int16_t rotation[3];
} ShCutsceneActorPose;

typedef struct ShCutsceneCameraPose
{
    int32_t eye[3];
    int32_t target[3];
    int16_t roll;
    int16_t projection;
} ShCutsceneCameraPose;

/* A cutscene file (game path, "ANIM/CAFE.DMS") was loaded into `slot`; ShCutscene_Start plays it there. */
void ShCutscene_Assign(uintptr_t slot, const char* game_path);
/* Starts what was assigned to `slot` (loads the JSON). 1 when the slot has a cutscene afterwards. */
int ShCutscene_Start(uintptr_t slot);
/* Pose of the actor track `name` (first four characters compared). 1 found; 0 no such track (pose zeroed);
 * -1 no cutscene for the slot (pose zeroed). */
int ShCutscene_ActorPose(uintptr_t slot, const char* name, int32_t time_q12, ShCutsceneActorPose* out);
/* Camera at `time_q12`. 1 done; -1 no cutscene (pose zeroed). */
int ShCutscene_CameraPose(uintptr_t slot, int32_t time_q12, ShCutsceneCameraPose* out);
/* Counts camera cuts: grows when ShCutscene_CameraPose samples another cutscene or another shot than the last time.
 * Frames presented between two game updates are not blended across a change of it. */
uint32_t ShCutscene_CameraCuts(void);
/* Timeline length in frames, -1 without a cutscene. */
int ShCutscene_FrameCount(uintptr_t slot);
/* Game path of what the slot plays or was assigned, "" when nothing. */
const char* ShCutscene_Path(uintptr_t slot);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_CUTSCENE_CUTSCENE_C_H */
