/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Vladislav Tislenko (keklick1337) */
/* Spawning any character on any map (console `spawn`, the debug menu, scripts' create
 * "spawn"): one entry point that loads what the character needs and picks the AI it runs where it stands. The
 * map's own room spawner never comes here. C-safe. */
#ifndef SH_ENGINE_CHARA_RUNTIME_CHARA_SPAWN_H
#define SH_ENGINE_CHARA_RUNTIME_CHARA_SPAWN_H

#ifdef __cplusplus
extern "C" {
#endif

/* The AI a spawn asks for. */
enum PcSpawnAi
{
    PcSpawnAi_Auto     = 0, /* the map's AI when the map runs the type, else character.json "spawn.auto" */
    PcSpawnAi_Full     = 1, /* the character's own behaviour on any map (missing map data: its Lua fallbacks) */
    PcSpawnAi_Fallback = 2, /* the generic chara/fallback_ai.lua */
    PcSpawnAi_None     = 3  /* a posed statue */
};

/* What a spawn runs on the current map (same numbers as sh::chara::behavior::SpawnRun). */
enum PcSpawnRun
{
    PcSpawnRun_Map      = 1,
    PcSpawnRun_Own      = 2,
    PcSpawnRun_Fallback = 3,
    PcSpawnRun_None     = 4
};

/* "auto" | "full" | "fallback" | "none" -> PcSpawnAi, -1 when unknown (case-insensitive). */
int Pc_Spawn_AiFind(const char* name);
/* PcSpawnAi -> its name ("auto" for anything unknown). */
const char* Pc_Spawn_AiName(int ai);
/* PcSpawnRun -> "map" | "own" | "fallback" | "none". */
const char* Pc_Spawn_RunName(int run);

/* What a spawn of the type asking for `ai` runs on the current map (PcSpawnRun). */
int Pc_Chara_AiResolve(int charaId, int ai);
/* character.json "spawn.kind", else "enemy" with a behaviour and "prop" without. Never NULL. */
const char* Pc_Chara_AiKind(int charaId);
/* 1 when an "auto" spawn of this type runs a behaviour on the current map. */
int Pc_Chara_HasAi(int charaId);
/* The first behaviour step of an "auto" spawn (character.json "spawn.state", else "params.spawn_state", else 3). */
int Pc_Chara_SpawnState(int charaId);
/* character.json "spawn.note", "" when none. Never NULL. */
const char* Pc_Chara_SpawnNote(int charaId);
/* 1 when the type can be created at all (its model/animation loaders know the number). */
int Pc_Chara_Spawnable(int charaId);

/* Creates the character at (x, z) on the ground facing `yaw` (Q12 angle), loading its model and animations when
 * the map did not; state < 0 = Pc_Chara_SpawnState; `ai` a PcSpawnAi. Returns the slot (1..) or -1. The spawn
 * carries no savegame identity. Pc_Spawn_Character asks for PcSpawnAi_Auto. */
int Pc_Spawn_CharacterAi(int charaId, int state, int x, int z, int yaw, int ai);
int Pc_Spawn_Character(int charaId, int state, int x, int z, int yaw);

#ifdef __cplusplus
}

namespace sh::chara {

// What a spawn needs from the owner of the character assets and NPC slots.
struct SpawnPorts {
    // Makes one type's assets resident (true: drawable).
    bool (*pool_ensure)(int chara_id) = nullptr;
    // Records what an engine spawn in NPC slot `slot` (1..) runs (PcSpawnRun).
    void (*set_spawn_run)(int slot, int run) = nullptr;
    // Makes Harry's reactions to the type's attacks resident (they may be clips of another map).
    void (*harry_reactions_ensure)(int chara_id, bool hostile) = nullptr;
    // Makes the type's sounds play on this map (its sound banks may be another map's ambient bank).
    void (*sound_banks_ensure)(int chara_id) = nullptr;
};
void install_spawn_ports(const SpawnPorts& ports);

}  // namespace sh::chara
#endif

#endif
