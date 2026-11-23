// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The part of the combat views (CombatView, MoveView) the engine answers from its own state: the characters'
// slot records (sh::entities::CharacterSlots in sh::game::Session: position, motion, health, flags, collision
// shape, attack block, damage) and the session's process state and gas weapon sound levels.
//
// The rest (the attack table, rays and wall tests, sounds, effects, the clip tables, the frame clock, options and
// cheats) is left to a subclass (src/engine/chara/game/combat_host.cpp).

#pragma once

#include "engine/combat/attack.h"
#include "engine/combat/movement.h"

namespace sh::entities {
struct CharacterRecord;
struct CharacterSlots;
}  // namespace sh::entities

namespace sh::combat {

class SessionCombatView : public CombatView, public MoveView {
public:
    // The characters' records (default: the session's).
    void set_storage(sh::entities::CharacterSlots* slots) { storage_ = slots; }

    // The record of a combat slot (kPlayerSlot or an NPC slot), nullptr for any other.
    sh::entities::CharacterRecord* record(int slot) const;
    // The slot of a record of the storage, -1 for anything else.
    int slot_of(const void* record) const;

    // The console's NOCOLLIDE of a character (left to the subclass: the console's picks).
    virtual bool no_collide(int slot) const = 0;
    // The combat traits of a character type (left to the subclass: the character data).
    virtual CombatTraits traits(int chara_id) const = 0;

    // CombatView
    bool in_gameplay() const override;
    bool game_over() const override;
    int target_npc() const override;
    bool foggy() const override;
    Combatant combatant(int slot) const override;
    AttackState attack_state(int slot) const override;
    void set_attack_state(int slot, const AttackState& state) override;
    SwingClock swing_clock(int slot) const override;
    bool attack_ticked(int slot) const override;
    void set_attack_ticked(int slot, bool ticked) override;
    void add_damage(int slot, std::int32_t amount, const Vec3& push) override;
    int attack_received(int slot) const override;
    void set_attack_received(int slot, int attack) override;
    void set_attacker(int slot, int attacker_npc) override;
    std::int32_t gas_sound_level(GasSound channel) const override;
    void set_gas_sound_level(GasSound channel, std::int32_t level) override;

    // MoveView
    MoverState mover(int slot) const override;
    void set_mover(int slot, const Vec3& position, std::int32_t fall_speed) override;
    ShapeOffsets shape_offsets(int slot) const override;
    void set_shape_offsets(int slot, const ShapeOffsets& offsets) override;
    std::int16_t yaw(int slot) const override;

private:
    sh::entities::CharacterSlots& storage() const;

    sh::entities::CharacterSlots* storage_ = nullptr;
};

}  // namespace sh::combat
