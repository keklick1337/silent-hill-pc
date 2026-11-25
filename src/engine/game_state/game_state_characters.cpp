// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The character slots of the game-state API (game_state.h): the player and NPC records of the session
// (sh::entities::CharacterSlots), the player's rig bones, the debug spawn and removal of NPCs, the ground under a
// point and the collision shape. Everything reads the engine's records; the ground query goes to the collision
// code (game_state_old_calls.h).

#include "engine/game_state/game_state.h"
#include "engine/game_state/game_state_old_calls.h"
#include "engine/entities/game/entity_host.h"
#include "engine/game_state/session.h"

#include "engine/entities/entity_world.h"

#include <cstring>

namespace sh::game_state {

namespace {

namespace ent = sh::entities;

ent::CharacterSlots& slots() { return sh::game::session().system.characters; }
ent::CharacterRecord* record(int slot) { return slots().record(slot); }

constexpr int kLegacyNpcSlots = 32;  // the room spawner's slots, the ones the NPC bit words cover

old_calls::GroundSurface surface_at(std::int32_t x, std::int32_t z) {
    old_calls::GroundSurface surface{};
    Collision_SurfaceGet(&surface, x, z);
    return surface;
}

void mark_npc_slot(int npc, bool live) {
    sh::game::SessionScene& scene = sh::game::session().system.scene;
    if (npc >= kLegacyNpcSlots) {
        return;
    }
    const std::uint32_t bit = 1u << npc;
    if (live) {
        scene.npc_flags = static_cast<std::int32_t>(static_cast<std::uint32_t>(scene.npc_flags) | bit);
        scene.spawned_groups = static_cast<std::int32_t>(static_cast<std::uint32_t>(scene.spawned_groups) | bit);
    } else {
        scene.npc_flags = static_cast<std::int32_t>(static_cast<std::uint32_t>(scene.npc_flags) & ~bit);
        scene.spawned_groups = static_cast<std::int32_t>(static_cast<std::uint32_t>(scene.spawned_groups) & ~bit);
    }
}

}  // namespace

static_assert(kNpcSlots == ent::kNpcSlotCount);

sh::entities::EntityWorld& characters() { return sh::entities::game::world(); }

int chara_id(int slot) {
    const ent::CharacterRecord* r = record(slot);
    return r != nullptr ? r->model.chara_id : chara::kNone;
}

std::int32_t chara_health(int slot) {
    const ent::CharacterRecord* r = record(slot);
    return r != nullptr ? r->health : 0;
}

void set_chara_health(int slot, std::int32_t health) {
    if (ent::CharacterRecord* r = record(slot)) {
        r->health = health;
    }
}

ent::Vec3 chara_position(int slot) {
    const ent::CharacterRecord* r = record(slot);
    return r != nullptr ? r->position : ent::Vec3{};
}

ent::Angles chara_rotation(int slot) {
    const ent::CharacterRecord* r = record(slot);
    return r != nullptr ? r->rotation : ent::Angles{};
}

void set_chara_position(int slot, const ent::Vec3& p) {
    if (ent::CharacterRecord* r = record(slot)) {
        r->position = p;
    }
}

void set_chara_rotation_y(int slot, std::int16_t yaw) {
    if (ent::CharacterRecord* r = record(slot)) {
        r->rotation.y = yaw;
    }
}

void chara_take_damage(int slot, std::int32_t amount) {
    if (ent::CharacterRecord* r = record(slot)) {
        r->damage.amount = amount;
    }
}

void chara_control(int slot, int* state, int* step) {
    const ent::CharacterRecord* r = record(slot);
    *state = r != nullptr ? r->model.control_state : 0;
    *step = r != nullptr ? r->model.control_step : 0;
}

PlayerAnimDebug player_anim_debug() {
    const ent::UpperBodyRecord& u = slots().player_upper;
    PlayerAnimDebug d;
    d.status = u.model.anim.status;
    d.flags = u.model.anim.flags;
    d.control = u.model.control_state;
    d.step = u.model.control_step;
    d.upper_body = u.upper_state;
    return d;
}

// ---- the ground ----

std::int32_t ground_height(std::int32_t x, std::int32_t z) { return surface_at(x, z).height; }

GroundSample ground_sample(std::int32_t x, std::int32_t z) {
    const old_calls::GroundSurface surface = surface_at(x, z);
    GroundSample g;
    g.height = surface.height;
    g.tilt_x = surface.tilt_x;
    g.tilt_z = surface.tilt_z;
    g.type = surface.type;
    return g;
}

void place_player(std::int32_t x, std::int32_t z, const std::int32_t* y) {
    ent::CharacterRecord& p = slots().player;
    const std::int32_t ground = surface_at(x, z).height;
    p.position.x = x;
    p.position.z = z;
    p.position.y = y != nullptr ? *y : ground;
    p.vars.harry.ground_height = ground;
    p.fall_speed = 0;
}

// ---- debug spawn and removal (the console's SPAWN) ----

bool despawn_npc(int slot) {
    if (slot < 1 || slot > ent::kNpcSlotCount) {
        return false;
    }
    const int npc = slot - 1;
    ent::CharacterRecord& r = slots().npcs[npc];
    if (r.model.chara_id == chara::kNone) {
        return false;
    }
    r.model.chara_id = chara::kNone;
    r.flags = 0;
    mark_npc_slot(npc, false);
    g_PcNpcDebugSpawned[npc] = 0;
    return true;
}

// ---- the player's rig bones ----

}  // namespace sh::game_state
