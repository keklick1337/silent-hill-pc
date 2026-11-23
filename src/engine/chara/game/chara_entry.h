/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Vladislav Tislenko (keklick1337) */
/* The C entry points of the character hosts (src/engine/chara/game, src/engine/entities/game) for the code that
 * holds character records and map headers: the update entries of the types whose behaviour is Lua, the
 * combat module over a record, the global asset pool and the engine spawn marks of the NPC slots. Records cross as
 * their C struct tags only. C-safe; includable inside or outside a C ABI region. */
#ifndef SH_ENGINE_CHARA_GAME_CHARA_ENTRY_H
#define SH_ENGINE_CHARA_GAME_CHARA_ENTRY_H

/* Pc_CharaSfx & co. (character data). */
#include "engine/chara/runtime/chara_data.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _SubCharacter;
struct _AnmHeader;
struct _GsCOORDINATE2;
struct _PlayerExtra;
struct ShScriptGameApi;

/* ---- Behaviour (sh::chara::behavior over the slots) ---- */

/* The map header's update entry of a type whose behaviour is gamedata/chara/<Name>/behavior.lua. */
void Pc_CharaBehavior_Scripted(struct _SubCharacter* npc, struct _AnmHeader* anm, struct _GsCOORDINATE2* bones);
/* The second update a map hands a type on the fly (map4_s03's Twinfeeler): sh.entity.update_variant is 1. */
void Pc_CharaBehavior_ScriptedAlt(struct _SubCharacter* npc, struct _AnmHeader* anm, struct _GsCOORDINATE2* bones);
/* A type that runs another type's behaviour (MapRuntime::chara_update_as: map7_s03's Incubator in
 * Unknown23's fight). */
void Pc_CharaBehavior_ScriptedAs(struct _SubCharacter* npc, struct _AnmHeader* anm, struct _GsCOORDINATE2* bones);
/* The generic fallback behaviour (chara/fallback_ai.lua). */
void Pc_CharaBehavior_Fallback(struct _SubCharacter* npc, struct _AnmHeader* anm, struct _GsCOORDINATE2* bones);

/* The NPC loop's call: runs `update` when there is one; 1 when it ran. */
int Pc_CharaBehavior_Run(struct _SubCharacter* npc, struct _AnmHeader* anm, struct _GsCOORDINATE2* bones,
                         void (*update)(struct _SubCharacter*, struct _AnmHeader*, struct _GsCOORDINATE2*));

/* The update NPC slot `npcIdx` (0-based) runs: the map header's entry, or for an engine spawn what its spawn
 * chose (the map's, its own behaviour, the fallback or none). NULL = a posed statue. */
typedef void (*Pc_CharaUpdateFn)(struct _SubCharacter*, struct _AnmHeader*, struct _GsCOORDINATE2*);
Pc_CharaUpdateFn Pc_CharaBehavior_ForSlot(int npcIdx);

/* Harry's legs and controller (pc_harry_movement.cpp, chara/Harry/behavior.lua and controller.lua). */
void Pc_HarryMovement_Update(struct _SubCharacter* player, struct _PlayerExtra* extra);
void Pc_HarryMovement_Footsteps(struct _SubCharacter* player, struct _PlayerExtra* extra);
void Pc_HarryController_Run(void);

/* The boss maps' move offset hook: Harry's collision moves the Split Head by (x, z),
 * Q12; the behaviour applies it on its next tick. */
void Pc_SplitHead_MoveOffsetAdd(int x, int z);
/* 1 when a Split Head off its boss maps (an engine spawn) pushed Harry since the last call: his tick then applies
 * the push as the boss map's pushing floor does. Clears it. */
int Pc_SplitHead_PushTake(void);

/* ---- Combat (sh::combat over a record) ---- */

/* sh::combat::update_attack: the character's attack this tick; returns its hit mask. */
int PcCombat_AttackUpdate(struct _SubCharacter* chara);
/* sh::combat::settle_attack: the end-of-tick pass that cancels an attack nobody updated. */
void PcCombat_AttackSettle(struct _SubCharacter* chara);
/* The start of the tick: the character's attack is not updated yet. */
void PcCombat_AttackTickReset(struct _SubCharacter* chara);
/* sh::combat::move_character without a push; returns the wall response code. */
int PcCombat_Move(struct _SubCharacter* chara);
/* The NPC index of the record; the player is 6 (one past the NPCs); -1 for any other record. */
int PcCombat_NpcIndexOf(const struct _SubCharacter* chara);
/* The gas weapon sound channels (sh::combat::gas_sound_*). Volumes Q8, negative: fade out. */
void PcCombat_GasSoundStopAll(void);
void PcCombat_GasSoundContact(int volume);
void PcCombat_GasSoundMotor(int chainsaw, int volume, int pitch);
void PcCombat_GasSoundDrillRun(int volume, int pitch);

/* ---- The character pool: assets of the types the map did not load ---- */

/* Loads one type's model, texture and animations on demand (again when a map's file table retargets them) and
 * registers them where the map left the registries empty or stale; 1 when drawable. */
int Pc_CharaPool_Ensure(int id);
/* 1 when the type's registered model is the pool's copy. */
int Pc_CharaPool_IsPoolModel(int charaId);

/* Per NPC slot: an engine spawn (no savegame identity: its partner byte is the slot, not a spawn row), and what it
 * runs (PcSpawnRun; 0 = not chosen). Cleared with every map load. */
extern unsigned char g_PcNpcDebugSpawned[];
extern unsigned char g_PcNpcSpawnAi[];
void Pc_NpcDebugSpawnClearAll(void);

#ifdef __cplusplus
}
#endif

#endif
