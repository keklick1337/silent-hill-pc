// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The Lua group sh.entity.* over EntityWorld and Character. A
// character is its slot (0 = the player, 1.. = the NPC slots).
//
// Core (bind_entity_script):
//   entity.create(chara, x, z, rot_y[, state[, ai]]) -> slot      entity.remove(slot) / exists(slot) / chara(slot)
//   entity.position / set_position / rotation / set_rotation     entity.play(slot, clip|index[, blend])
//   entity.anim(slot) -> clip, playing, keyframe, time            entity.anim_set(slot, status[, time])
//   entity.clip(slot, name) / keyframe_passed(slot, kf, prev)     entity.bone_count(slot) / bone(slot, i)
//   entity.sound(slot, name[, fallback]) / param(slot, name[, fallback]) / health / set_health
//   entity.has_ai(slot) / engine_spawn(slot) / spawn_kind(slot)
// Enemy group (bind_entity_enemy_script), the calls the enemy behaviours stand on:
//   entity.get / entity.set(slot, field[, value]) for the record fields named in entity_script.cpp (others fall
//   through to the next binding), entity.sys / set_sys(name, ...), entity.props_clear(slot), and the sight,
//   steering, sound, clip table and bone calls the EnemyHost answers.

#pragma once

#include "engine/script/sh_script.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace sh::entities {

// What the core calls need from the game beyond EntityWorld (the game's spawn code and its behaviour table;
// src/engine/entities/game/entity_host.cpp).
struct EntityScriptHost {
    virtual ~EntityScriptHost() = default;
    // characters.json "enum"/name -> id, -1 when unknown.
    virtual int chara_id(const char* name) = 0;
    // The ai mode of a spawn by name ("auto", "full", "fallback", "none"), -1 when unknown; the default for none.
    virtual int ai_mode(const char* name) = 0;
    // Spawns through the game's spawn path (model and animations load when the map did not). Returns the slot.
    virtual int spawn(int chara_id, int state, int x, int z, int rot_y, int ai_mode) = 0;
    // The NPC in `slot` runs a behaviour this tick.
    virtual bool has_behavior(int slot) = 0;
    // What an engine spawn of `slot` chose (1 map, 2 own, 3 fallback, 4 none); 0 for the map's own spawns.
    virtual int spawn_choice(int slot) = 0;
    virtual bool engine_spawned(int slot) = 0;
    // characters.json "spawn.kind": "enemy", "boss", "actor" or "prop".
    virtual std::string_view spawn_kind(int chara_id) = 0;
};

// What the enemy calls need from the game (sight lines, steering, sounds, the clip tables and the bone math;
// src/engine/chara/game/combat_host.cpp).
struct EnemyHost {
    virtual ~EnemyHost() = default;
    virtual bool player_visible(int slot, std::int32_t mode, std::int32_t distance, std::int32_t angle) = 0;
    virtual std::int32_t player_noise(int slot, std::int32_t distance, std::int32_t angle) = 0;
    virtual bool player_blocked(int slot) = 0;
    virtual bool path_blocked(int slot, std::int32_t distance, std::int32_t angle) = 0;
    virtual bool path_hit(int slot, std::int32_t distance, std::int32_t angle) = 0;
    virtual std::int32_t clear_heading(int slot, std::int32_t distance, std::int32_t angle) = 0;
    virtual std::int32_t heading_toward(int slot, std::int32_t distance, std::int32_t x, std::int32_t z,
                                        std::int32_t span, bool nearest) = 0;
    // The map's saved enemy row of the character is cleared (it does not come back).
    virtual void enemy_defeated(int slot) = 0;
    // The death/recoil hand-over of the clip.
    virtual void anim_status_set(int slot, std::int32_t status, std::int32_t keyframe) = 0;
    virtual void sfx_3d(int slot, int sfx, std::int32_t volume_q8, std::int32_t kind) = 0;
    virtual void sfx_falloff(int slot, int sfx, std::int32_t volume, std::int32_t falloff_q12, std::int32_t pitch) = 0;

    // Clip tables: false when the character has none (or the status is out of range).
    virtual bool clip_duration(int slot, int status, std::int32_t* speed) = 0;
    virtual bool clip_bound_set(int slot, int status, bool end, std::int32_t keyframe) = 0;
    virtual bool clip_table_attach(int slot, int index) = 0;

    // Bones the type's update poses; -1: none.
    virtual int bone_count(int slot) = 0;
    virtual void root_transform(int slot) = 0;
    virtual void bone_rotate(int slot, int bone, std::int16_t rx, std::int16_t ry, std::int16_t rz) = 0;
    virtual std::array<std::int32_t, 3> bone_point(int slot, int bone, std::int16_t x, std::int16_t y,
                                                   std::int16_t z) = 0;
};

// Put the groups in front of the API's named calls (one binding each per process; the hosts must outlive it).
void bind_entity_script(ShScriptGameApi& api, EntityScriptHost& host);
void bind_entity_enemy_script(ShScriptGameApi& api, EnemyHost& host);

}  // namespace sh::entities
