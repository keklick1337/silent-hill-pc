// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The console's byte form of the character records: where each value of a
// CharacterRecord and of the player's upper body sat in the PS1's memory. The engine keeps these records by name
// (character_record.h); one thing still needs the console's bytes and reads them through this table: slot -1
// (unset_slot below). The original reads an unset NPC index (Harry's grabber or attacker) as the record one before
// the NPC table, which on the console is the tail of the player's record and his upper body.
// Pointers have no console value: they read as zero and are never written.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/entities/character_record.h"
#include "engine/save/console_fields.h"

namespace sh::entities::console {

inline constexpr std::size_t kCharacterBytes = 0x128;
inline constexpr std::size_t kUpperBodyBytes = 0x2C;

namespace detail {

template <class Io, class Model>
void visit_model(const Io& io, std::size_t at, Model& m) {
    io.template narrow<std::int8_t>(at + 0x00, m.chara_id);
    io.field(at + 0x01, m.palette);
    io.field(at + 0x02, m.control_state);
    io.field(at + 0x03, m.control_step);
    io.field(at + 0x04, m.anim.status);
    io.field(at + 0x05, m.anim.map_status_start);
    io.field(at + 0x06, m.anim.flags);
    io.field(at + 0x08, m.anim.time);
    io.field(at + 0x0C, m.anim.keyframe);
    io.field(at + 0x0E, m.anim.alpha);
}

template <class Io, class V>
void visit_vec3(const Io& io, std::size_t at, V& v) {
    io.field(at + 0, v.x);
    io.field(at + 4, v.y);
    io.field(at + 8, v.z);
}

template <class Io, class A>
void visit_angles(const Io& io, std::size_t at, A& a) {
    io.field(at + 0, a.x);
    io.field(at + 2, a.y);
    io.field(at + 4, a.z);
}

}  // namespace detail

template <class Io>
void visit_character(const Io& io, std::size_t at, save::console::Visited<Io, CharacterRecord>& r) {
    detail::visit_model(io, at, r.model);
    detail::visit_vec3(io, at + 0x18, r.position);
    detail::visit_angles(io, at + 0x24, r.rotation);
    io.field(at + 0x2A, r.angle_to_target);
    detail::visit_angles(io, at + 0x2C, r.rotation_speed);
    io.field(at + 0x32, r.angle_to_target_speed);
    io.field(at + 0x34, r.fall_speed);
    io.field(at + 0x38, r.move_speed);
    io.field(at + 0x3C, r.heading);
    io.field(at + 0x3E, r.flags);
    io.template narrow<std::int8_t>(at + 0x40, r.partner);
    io.template narrow<std::int8_t>(at + 0x41, r.attack_received);
    auto& k = r.attack;
    const std::size_t a = at + 0x44;
    io.field(a + 0x00, k.phase);
    io.template narrow<std::uint8_t>(a + 0x02, k.attack);
    io.field(a + 0x03, k.shots_left);
    io.field(a + 0x04, k.shots_fired);
    io.field(a + 0x05, k.swing_status);
    io.field(a + 0x06, k.swing_keyframe);
    io.field(a + 0x08, k.hit_mask);
    io.field(a + 0x0C, k.aim_yaw);
    io.field(a + 0x0E, k.aim_pitch);
    io.field(a + 0x10, k.prev_aim_yaw);
    io.field(a + 0x12, k.prev_aim_pitch);
    io.field(a + 0x14, k.checked_time);
    detail::visit_vec3(io, a + 0x18, k.origin);
    detail::visit_vec3(io, a + 0x24, k.segment_start[0]);
    detail::visit_vec3(io, a + 0x30, k.segment_start[1]);
    detail::visit_vec3(io, a + 0x3C, k.prev_origin);
    detail::visit_vec3(io, a + 0x48, k.segment[0]);
    detail::visit_vec3(io, a + 0x54, k.segment[1]);
    detail::visit_vec3(io, a + 0x60, k.segment[2]);
    io.field(at + 0xB0, r.health);
    detail::visit_vec3(io, at + 0xB4, r.damage.push);
    io.field(at + 0xC0, r.damage.amount);
    io.field(at + 0xC4, r.death_timer);
    io.field(at + 0xC6, r.timer);
    auto& c = r.collision;
    io.field(at + 0xC8, c.box.top);
    io.field(at + 0xCA, c.box.bottom);
    io.field(at + 0xCC, c.box.height);
    io.field(at + 0xCE, c.box.offset_y);
    io.field(at + 0xD0, c.box.extent_x);
    io.field(at + 0xD2, c.box.extent_z);
    io.field(at + 0xD4, c.cylinder.radius);
    io.field(at + 0xD6, c.cylinder.chara_radius);
    io.field(at + 0xD8, c.offsets.box.x);
    io.field(at + 0xDA, c.offsets.box.z);
    io.field(at + 0xDC, c.offsets.cylinder.x);
    io.field(at + 0xDE, c.offsets.cylinder.z);
    io.field(at + 0xE0, c.pass_stamp);
    io.field(at + 0xE1, c.state_and_count);
    for (std::size_t i = 0; i < kCharacterVarBytes; ++i) {
        io.field(at + 0xE8 + i, r.vars.bytes[i]);
    }
}

template <class Io>
void visit_upper_body(const Io& io, std::size_t at, save::console::Visited<Io, UpperBodyRecord>& u) {
    detail::visit_model(io, at, u.model);
    io.field(at + 0x18, u.disabled_bones);
    io.field(at + 0x1C, u.state);
    io.field(at + 0x20, u.upper_state);
    io.field(at + 0x24, u.lower_state);
    io.field(at + 0x28, u.last_used_item);
}

// Slot -1 as the original reads it: a record decoded from the console bytes one record before the NPC table.
// unset_slot() refreshes the view from `slots` and returns it; unset_slot_commit() writes the view's values back
// into the player's record and upper body, where those bytes live.
CharacterRecord& unset_slot(const CharacterSlots& slots);
void unset_slot_commit(CharacterSlots& slots);

}  // namespace sh::entities::console
