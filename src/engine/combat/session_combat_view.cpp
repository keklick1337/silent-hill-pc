// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/combat/session_combat_view.h"

#include "engine/entities/character_record.h"
#include "engine/game_state/session.h"

namespace sh::combat {
namespace {

namespace ent = sh::entities;

Vec3 vec(const ent::Vec3& v) { return Vec3{v.x, v.y, v.z}; }
ent::Vec3 vec(const Vec3& v) { return ent::Vec3{v.x, v.y, v.z}; }

std::int32_t& gas_level(GasSound channel) {
    sh::game::SessionAudio& audio = sh::game::session().system.audio;
    switch (channel) {
        case GasSound::Contact: return audio.gas_weapon_contact_level;
        case GasSound::RockDrillRun: return audio.rock_drill_run_level;
        default: return audio.gas_weapon_engine_level;  // the chainsaw and the drill's start share it
    }
}

}  // namespace

ent::CharacterSlots& SessionCombatView::storage() const {
    return storage_ != nullptr ? *storage_ : sh::game::session().system.characters;
}

ent::CharacterRecord* SessionCombatView::record(int slot) const {
    if (slot == kPlayerSlot) {
        return &storage().player;
    }
    return slot >= 1 && slot <= ent::kNpcSlotCount ? &storage().npcs[slot - 1] : nullptr;
}

int SessionCombatView::slot_of(const void* record) const {
    const int slot = storage().slot_of(record);
    return slot == 0 ? kPlayerSlot : slot;
}

bool SessionCombatView::in_gameplay() const { return sh::game::session().sys_state() == sh::game::sys_state::kGameplay; }

bool SessionCombatView::game_over() const { return sh::game::session().sys_state() == sh::game::sys_state::kGameOver; }

int SessionCombatView::target_npc() const { return sh::game::session().system.target_npc; }

bool SessionCombatView::foggy() const {
    return (sh::game::session().system.environment.current.preset.flags & (1 << 0)) != 0;
}

Combatant SessionCombatView::combatant(int slot) const {
    Combatant c;
    const ent::CharacterRecord* r = record(slot);
    if (r == nullptr) {
        return c;
    }
    c.present = r->model.chara_id != 0;
    c.chara_id = r->model.chara_id;
    c.position = vec(r->position);
    c.health = r->health;
    c.downed = (r->flags & ent::kFlagDowned) != 0;
    c.recoiling = (r->flags & ent::kFlagHit) != 0;
    c.collidable = r->collision.state() != 0;
    c.box_top = r->collision.box.top;
    c.box_height = r->collision.box.height;
    c.radius = r->collision.cylinder.chara_radius;
    c.box_offset_x = r->collision.offsets.box.x;
    c.box_offset_z = r->collision.offsets.box.z;
    c.no_collide = no_collide(slot);
    c.traits = traits(c.chara_id);
    return c;
}

AttackState SessionCombatView::attack_state(int slot) const {
    AttackState s;
    const ent::CharacterRecord* r = record(slot);
    if (r == nullptr) {
        return s;
    }
    const ent::AttackRecord& a = r->attack;
    s.phase = a.phase;
    s.attack = a.attack;
    s.shots_left = a.shots_left;
    s.shots_fired = a.shots_fired;
    s.swing_status = a.swing_status;
    s.swing_keyframe = a.swing_keyframe;
    s.hit_mask = a.hit_mask;
    s.aim_yaw = a.aim_yaw;
    s.aim_pitch = a.aim_pitch;
    s.prev_aim_yaw = a.prev_aim_yaw;
    s.prev_aim_pitch = a.prev_aim_pitch;
    s.checked_time = a.checked_time;
    s.origin = vec(a.origin);
    s.segment_start[0] = vec(a.segment_start[0]);
    s.segment_start[1] = vec(a.segment_start[1]);
    s.prev_origin = vec(a.prev_origin);
    for (int i = 0; i < 3; ++i) {
        s.segment[i] = vec(a.segment[i]);
    }
    return s;
}

void SessionCombatView::set_attack_state(int slot, const AttackState& s) {
    ent::CharacterRecord* r = record(slot);
    if (r == nullptr) {
        return;
    }
    ent::AttackRecord& a = r->attack;
    a.phase = s.phase;
    a.attack = s.attack;
    a.shots_left = s.shots_left;
    a.shots_fired = s.shots_fired;
    a.swing_status = s.swing_status;
    a.swing_keyframe = s.swing_keyframe;
    a.hit_mask = s.hit_mask;
    a.aim_yaw = s.aim_yaw;
    a.aim_pitch = s.aim_pitch;
    a.prev_aim_yaw = s.prev_aim_yaw;
    a.prev_aim_pitch = s.prev_aim_pitch;
    a.checked_time = s.checked_time;
    a.origin = vec(s.origin);
    a.segment_start[0] = vec(s.segment_start[0]);
    a.segment_start[1] = vec(s.segment_start[1]);
    a.prev_origin = vec(s.prev_origin);
    for (int i = 0; i < 3; ++i) {
        a.segment[i] = vec(s.segment[i]);
    }
}

SwingClock SessionCombatView::swing_clock(int slot) const {
    // The player's attack is timed by his upper body, an NPC's by its whole body.
    const ent::ModelAnimRecord* anim = nullptr;
    if (slot == kPlayerSlot) {
        anim = &storage().player_upper.model.anim;
    } else if (const ent::CharacterRecord* r = record(slot)) {
        anim = &r->model.anim;
    }
    return anim != nullptr ? SwingClock{anim->status, anim->time} : SwingClock{};
}

bool SessionCombatView::attack_ticked(int slot) const {
    const ent::CharacterRecord* r = record(slot);
    return r != nullptr && (r->flags & ent::kFlagAttackTicked) != 0;
}

void SessionCombatView::set_attack_ticked(int slot, bool ticked) {
    if (ent::CharacterRecord* r = record(slot)) {
        r->flags = static_cast<std::int16_t>(ticked ? (r->flags | ent::kFlagAttackTicked)
                                                    : (r->flags & ~ent::kFlagAttackTicked));
    }
}

void SessionCombatView::add_damage(int slot, std::int32_t amount, const Vec3& push) {
    if (ent::CharacterRecord* r = record(slot)) {
        r->damage.amount += amount;
        r->damage.push.x += push.x;
        r->damage.push.y += push.y;
        r->damage.push.z += push.z;
    }
}

int SessionCombatView::attack_received(int slot) const {
    const ent::CharacterRecord* r = record(slot);
    return r != nullptr ? r->attack_received : -1;
}

void SessionCombatView::set_attack_received(int slot, int attack) {
    if (ent::CharacterRecord* r = record(slot)) {
        r->attack_received = attack;
    }
}

void SessionCombatView::set_attacker(int slot, int attacker_npc) {
    if (ent::CharacterRecord* r = record(slot)) {
        r->partner = attacker_npc;
    }
}

std::int32_t SessionCombatView::gas_sound_level(GasSound channel) const { return gas_level(channel); }

void SessionCombatView::set_gas_sound_level(GasSound channel, std::int32_t level) { gas_level(channel) = level; }

MoverState SessionCombatView::mover(int slot) const {
    MoverState m;
    if (const ent::CharacterRecord* r = record(slot)) {
        m.position = vec(r->position);
        m.move_speed = r->move_speed;
        m.heading = r->heading;
        m.fall_speed = r->fall_speed;
    }
    return m;
}

void SessionCombatView::set_mover(int slot, const Vec3& position, std::int32_t fall_speed) {
    if (ent::CharacterRecord* r = record(slot)) {
        r->position = vec(position);
        r->fall_speed = fall_speed;
    }
}

MoveView::ShapeOffsets SessionCombatView::shape_offsets(int slot) const {
    ShapeOffsets o;
    if (const ent::CharacterRecord* r = record(slot)) {
        const ent::CollisionShapeOffsets& s = r->collision.offsets;
        o.box = PlanarOffset{s.box.x, s.box.z};
        o.cylinder = PlanarOffset{s.cylinder.x, s.cylinder.z};
    }
    return o;
}

void SessionCombatView::set_shape_offsets(int slot, const ShapeOffsets& o) {
    if (ent::CharacterRecord* r = record(slot)) {
        ent::CollisionShapeOffsets& s = r->collision.offsets;
        s.box = ent::PlanarOffset{o.box.x, o.box.z};
        s.cylinder = ent::PlanarOffset{o.cylinder.x, o.cylinder.z};
    }
}

std::int16_t SessionCombatView::yaw(int slot) const {
    const ent::CharacterRecord* r = record(slot);
    return r != nullptr ? r->rotation.y : 0;
}

}  // namespace sh::combat
