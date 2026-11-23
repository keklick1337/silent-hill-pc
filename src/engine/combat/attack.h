// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Attacks of characters on each other: the hit window of an attack animation, the sweep of a
// melee weapon through the target's volume, the rays of a gun, the damage and push a hit deals, the impact sounds
// and the blood. Written from the behaviour of the original game; the
// results are the same integers.
//
// The module keeps no game state. Characters, their attack state, the world's rays and the effects are reached
// through CombatView, which the game implements over its records (src/engine/chara/game/combat_host.cpp). A
// character is its slot: 0 the player, 1..6 the NPCs.

#pragma once

#include "engine/combat/combat_math.h"
#include "engine/items/item_library.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace sh::combat {

constexpr int kPlayerSlot = 0;
constexpr int kNpcSlots = 6;

// Phase of AttackState::phase.
namespace attack_phase {
constexpr std::int16_t kIdle = 0;
constexpr std::int16_t kRequested = 1;
constexpr std::int16_t kFirstTick = 2;
constexpr std::int16_t kSweeping = 3;
constexpr std::int16_t kFinished = -1;
}  // namespace attack_phase

// The attack a character is performing.
struct AttackState {
    std::int16_t phase = 0;        // attack_phase
    std::int32_t attack = 0;       // packed attack number (row of the attack table)
    std::uint8_t shots_left = 0;   // guns: 100 at the start, one less per shot
    std::uint8_t shots_fired = 0;  // guns: compared with the row's max_shots
    // The animation status and whole keyframe the last request saw: a new swing re-arms the hit mask.
    std::uint8_t swing_status = 0;
    std::uint8_t swing_keyframe = 0;
    std::int32_t hit_mask = 0;     // bit 1 << npc per NPC hit by this attack, all bits when the player was hit
    std::int16_t aim_yaw = 0;
    std::int16_t aim_pitch = 0;    // from the vertical: 1024 is level
    std::int16_t prev_aim_yaw = 0;
    std::int16_t prev_aim_pitch = 0;
    std::int32_t checked_time = 0;  // animation time (Q12 keyframes) up to which hits were checked
    Vec3 origin;                    // where the swing or the shot starts
    Vec3 segment_start[2];          // near end of the hit segment: [0] this step, [1] the previous one
    Vec3 prev_origin;
    // The hit segment, near to far end: [0] this step, [1] the previous one (a gun keeps the unit direction of its
    // last hitting pellet in [1]). [2] is the far end of the previous segment from prev_origin; never written.
    Vec3 segment[3];
};

// What the combat rules read of a character's type (gamedata/chara/<Name>/character.json); the
// defaults are a type the data says nothing about.
struct CombatTraits {
    int blood_kind = 16;                  // colour kind of the blood a hit spills, -1 none ("blood")
    bool blood_at_zero_health = false;    // health exactly 0 still bleeds
    int blood_strength = -1;              // >= 0 replaces the weapon row's blood strength
    int fresh_hit_blood_strength = -1;    // >= 0: the strength of a hit while not recoiling
    int fresh_hit_blood_strength_scatter = -1;  // the same for a scatter shot (a ray of several pellets)
    std::string impact_sound;             // a melee hit's sound instead of the weapon's; empty: the weapon's
    bool is_player = false;               // aims with the aim timer, rumbles, counts in the statistics
    bool enemy = false;                   // a common enemy: its distance steadies a single-ray gun's aim
};

// What the combat code reads of a character.
struct Combatant {
    bool present = false;            // a character is in the slot
    int chara_id = 0;                // sh::game_state::chara number
    Vec3 position;
    std::int32_t health = 0;
    bool downed = false;             // lying on the ground (kicks and stomps kill it outright)
    bool recoiling = false;          // already hit and recoiling (a fresh melee hit deals four times the damage)
    bool collidable = false;
    std::int16_t box_top = 0;        // collision box, relative to the feet (negative: up)
    std::int16_t box_height = 0;
    std::int16_t radius = 0;         // character-to-character radius
    std::int16_t box_offset_x = 0;   // the box's offset from the position, already turned by the heading
    std::int16_t box_offset_z = 0;
    bool no_collide = false;         // console NOCOLLIDE: attacks pass through
    CombatTraits traits;             // of its type
};

// The animation an attack is timed by (the player's upper body, an NPC's whole body).
struct SwingClock {
    std::int32_t status = 0;
    std::int32_t time = 0;  // Q12 keyframes
};

// A gun ray's result.
struct ShotTrace {
    bool hit = false;
    Vec3 point;
    bool character = false;  // a character stopped the ray (else the world did)
    int target_slot = -1;    // its slot, -1 when it is none of the player and the NPCs
};

// The gas weapons' sound channels (chainsaw and rock drill).
enum class GasSound : std::uint8_t {
    Contact,         // the blade or bit grinding on a target
    ChainsawRun,
    RockDrillStart,  // shares its level with ChainsawRun
    RockDrillRun,
};

// The kinds of finished attack the ranking statistics count (the numbers are the game's).
enum class AttackStat : std::int8_t {
    None = -1,
    CloseShot = 0,    // handgun or rifle at the target (within 2 m)
    NearShot = 1,     // within a third of the gun's reach
    FarShot = 2,
    FogShot = 3,      // fired in fog
    HyperBlaster = 4,
    OtherShot = 5,    // shotgun, or a handgun/rifle shot with no target
    Melee = 6,
    HeavyMelee = 7,   // katana, chainsaw, rock drill
};

// The game state and side effects the combat code reaches. Implemented by the game over its records.
class CombatView {
public:
    virtual ~CombatView() = default;

    // Frame and process state.
    virtual std::int32_t delta_time() const = 0;     // Q12 seconds
    virtual bool in_gameplay() const = 0;            // the gameplay loop runs (no menu, cutscene or game over)
    virtual bool game_over() const = 0;
    virtual bool room_fading_in() const = 0;         // the reveal after a load: no hit lands
    virtual bool no_target_cheat() const = 0;        // nothing hits the player
    virtual bool alternate_camera() const = 0;       // a third/first person camera (shots are capped)
    virtual bool bullet_fat_hitbox() const = 0;      // options
    virtual bool melee_once_per_swing() const = 0;   // GAMEPLAY CHANGE option
    virtual int target_npc() const = 0;              // NPC index the player aims at, -1 none
    virtual bool foggy() const = 0;                  // the area's fog effect is on
    virtual std::int32_t aim_time() const = 0;       // how long the player has aimed (Q12 seconds)
    virtual int hyper_blaster_color() const = 0;
    virtual std::int32_t random_q12() = 0;           // the game's random number, 0..4095
    virtual sh::items::WeaponAttack attack_row(int attack) const = 0;
    virtual int attack_count() const = 0;  // rows of the attack table
    // The row named `name` in the attack table ("StalkerLunge"), -1 when none.
    virtual int attack_number(std::string_view name) const {
        (void)name;
        return -1;
    }

    // Characters.
    virtual Combatant combatant(int slot) const = 0;
    virtual AttackState attack_state(int slot) const = 0;
    virtual void set_attack_state(int slot, const AttackState& state) = 0;
    virtual SwingClock swing_clock(int slot) const = 0;
    virtual std::int32_t swing_start_keyframe(int slot) const = 0;  // first keyframe of the clip playing
    // Points the player's clip tables at Harry's base and map clips before his attack is timed.
    virtual void bind_player_clips() = 0;
    virtual bool attack_ticked(int slot) const = 0;  // the attack was updated this tick
    virtual void set_attack_ticked(int slot, bool ticked) = 0;
    virtual void add_damage(int slot, std::int32_t amount, const Vec3& push) = 0;
    virtual int attack_received(int slot) const = 0;
    virtual void set_attack_received(int slot, int attack) = 0;
    virtual void set_attacker(int slot, int attacker_npc) = 0;  // the player's record of who hit him

    // World.
    virtual ShotTrace trace_shot(const Vec3& from, const Vec3& offset, int exclude_slot, bool fat_hitbox) = 0;
    virtual bool line_blocked(const Vec3& from, const Vec3& to) = 0;  // walls between two points

    // Effects.
    virtual void rumble(int pattern) = 0;
    virtual void motor_rumble(bool chainsaw) = 0;
    virtual void play_sound(std::string_view name, const Vec3& position, std::int32_t volume_q8) = 0;
    virtual void blood(int slot, const Vec3& position, int strength, int kind) = 0;
    virtual void bullet_impact(const Vec3& point, const Vec3& direction, const Vec3& origin) = 0;
    virtual void count_attack(AttackStat stat, bool hit, std::int32_t aim_time) = 0;

    // The gas weapon sound channels: the level (Q12, 256 + volume while audible) is kept by the game.
    virtual std::int32_t gas_sound_level(GasSound channel) const = 0;
    virtual void set_gas_sound_level(GasSound channel, std::int32_t level) = 0;
    virtual void gas_sound_start(GasSound channel, std::int32_t volume, std::int32_t pitch) = 0;
    virtual void gas_sound_keep(GasSound channel, std::int32_t volume, std::int32_t pitch) = 0;
    virtual void gas_sound_stop(GasSound channel) = 0;
};

// The hit window of an attack in whole keyframes from the start of its animation; a running gas weapon has its
// own (the motor makes it bite at once and for longer).
int hit_window_start(const CombatView& view, int attack);
int hit_window_length(const CombatView& view, int attack);
// `keyframe` lies in the row's own window.
bool in_hit_window(const sh::items::WeaponAttack& row, std::int32_t keyframe);

// A character requests or holds its attack this tick: `request` > 0 starts or holds it, <= 0 cancels it. `origin`
// is where the swing or shot starts, `yaw` and `pitch` its direction. `victim` is the character the caller attacks
// (the no-target cheat spares the player). Returns, for the player, 6 - the index of the first NPC hit; for an NPC,
// 0 when the player was hit by anything this tick; -1 otherwise.
int request_attack(CombatView& view, int request, int attack, int slot, const Vec3& origin, int victim,
                   std::int16_t yaw, std::int16_t pitch);

// Advances the character's attack through its hit window and resolves the hits. Returns the hit mask.
std::int32_t update_attack(CombatView& view, int slot);

// The end-of-tick pass: an attack nobody updated this tick is cancelled.
void settle_attack(CombatView& view, int slot);

// The gas weapon sounds: `volume` Q8 starts or holds the channel (0 stops it), a negative one fades it out.
// Returns whether the channel is audible.
bool gas_sound_update(CombatView& view, GasSound channel, std::int32_t volume, std::int32_t pitch);
void gas_sound_contact(CombatView& view, std::int32_t volume);
void gas_sound_drill_run(CombatView& view, std::int32_t volume, std::int32_t pitch);
void gas_sound_motor(CombatView& view, bool chainsaw, std::int32_t volume, std::int32_t pitch);
void gas_sound_stop_all(CombatView& view);

}  // namespace sh::combat
