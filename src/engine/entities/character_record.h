// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The storage of the characters of the running game: the player's record and his
// upper body, the NPC records and the bone transforms, held by sh::game::Session (session().system.characters).
// sh::entities::Character binds to a slot of it and reads and writes these fields directly.
//
// These are the engine's own records: nothing reads them as bytes. The console's layout of them lives only in
// console_character.h, through which the unset slot -1 reads them.
//
// Units are the game's: positions Q12 metres (+Y down), angles Q12 turns, time Q12 seconds.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/camera/view_system.h"

namespace sh::entities {

struct Vec3 {
    std::int32_t x = 0, y = 0, z = 0;  // Q12 metres, +Y down
    bool operator==(const Vec3&) const = default;
};

struct Angles {
    std::int16_t x = 0, y = 0, z = 0;  // Q12 turns (4096 = 360 degrees)
    bool operator==(const Angles&) const = default;
};

// Bits of CharacterRecord::flags, by meaning.
enum CharacterFlag : std::uint16_t {
    kFlagPadlockBroken = 1u << 0,  // the padlock prop only
    kFlagDowned = 1u << 1,         // on the ground: the player's attacks go low, the camera ignores it
    kFlagHit = 1u << 2,            // a hit that makes it recoil landed this tick
    kFlagAttackVeto = 1u << 3,     // the player: a timed state in which nothing attacks him
    kFlagCameraExcluded = 1u << 4, // the camera does not frame it
    kFlagDamaged = 1u << 5,
    kFlagDead = 1u << 6,
    kFlagAttackTicked = 1u << 7,   // its attack was updated this tick
    kFlagNoRadioStatic = 1u << 8,  // the radio stays silent for it
};

// The animation state of a model (clip status, timeline, blend) and its clip tables.
struct ModelAnimRecord {
    std::uint8_t status;            // bit 0: playing (not blending), bits 1..7: clip index
    std::uint8_t map_status_start;  // the player: status of the first map clip
    std::uint16_t flags;            // kAnimAdvances / kAnimPosed in the low bits
    std::int32_t time;              // Q12 keyframes
    std::int16_t keyframe;
    std::int16_t alpha;             // Q12 blend progress
    const void* clip_table;         // the type's clip table (the player: base clips)
    const void* map_clip_table;     // the player: the map's clips (from map_status_start on)
};

struct ModelRecord {
    std::int32_t chara_id;  // the character's run-time number (CharaLibrary); 0: the slot is empty
    std::uint8_t palette;
    std::uint8_t control_state;
    std::uint8_t control_step;
    ModelAnimRecord anim;
};

// The attack a character performs (combat module, sh::combat::AttackState).
struct AttackRecord {
    std::int16_t phase;  // 0 idle, 1 requested, 2 first tick, 3 hit checks, -1 finished
    std::int32_t attack;  // packed weapon attack (row of the attack table)
    std::uint8_t shots_left;
    std::uint8_t shots_fired;
    std::uint8_t swing_status;    // the clip status the swing started in
    std::uint8_t swing_keyframe;  // the keyframe it started at
    std::int32_t hit_mask;  // bit per NPC hit; -1: the player was hit
    std::int16_t aim_yaw;
    std::int16_t aim_pitch;
    std::int16_t prev_aim_yaw;
    std::int16_t prev_aim_pitch;
    std::int32_t checked_time;  // animation time the hit checks have run up to
    Vec3 origin;
    Vec3 segment_start[2];  // this tick, the previous one
    Vec3 prev_origin;
    Vec3 segment[3];        // this tick, the previous one, and one nothing writes
};

// Damage received this tick: the push and the amount (Q12).
struct DamageRecord {
    Vec3 push;
    std::int32_t amount;
};

struct CollisionBox {
    std::int16_t top;
    std::int16_t bottom;
    std::int16_t height;
    std::int16_t offset_y;
    std::int16_t extent_x;
    std::int16_t extent_z;
};

struct CollisionCylinder {
    std::int16_t radius;        // against the map
    std::int16_t chara_radius;  // against other characters
};

struct PlanarOffset {
    std::int16_t x;
    std::int16_t z;
};

struct CollisionShapeOffsets {
    PlanarOffset box;
    PlanarOffset cylinder;
};

struct CollisionRecord {
    CollisionBox box;
    CollisionCylinder cylinder;
    CollisionShapeOffsets offsets;
    std::uint8_t pass_stamp;  // the collision pass that last met it (the player: knocked an enemy down)
    // Low nibble (signed): the collision state, 0 = no collision; high nibble: how many records follow.
    std::uint8_t state_and_count;
    const void* records;  // the type's extra collider parts (the Twinfeeler's), nullptr for the rest

    int state() const { return static_cast<std::int8_t>(static_cast<std::uint8_t>(state_and_count << 4)) >> 4; }
    void set_state(int s) {
        state_and_count = static_cast<std::uint8_t>((state_and_count & 0xF0) | (static_cast<unsigned>(s) & 0x0F));
    }
};

// Harry's values in his variable store (CharacterVars::harry).
struct HarryVars {
    std::int32_t afk_timer;      // idle time before the idle clip (Q12 s)
    std::int32_t ground_height;
    std::int32_t flex_source;    // nothing reads it for Harry; scenes clear it with the flex angle
    std::int32_t flex_target_x;
    std::int32_t run_steps;
    std::int32_t exhaustion;
    std::int32_t aim_hold;
    std::int32_t attack_time;
    std::int32_t run_ahead;
    std::uint8_t step_sound;
    std::uint8_t fall_direction;
    std::int32_t flag_timer;
    std::int32_t gas_weapon_timer;
    std::int16_t push_angle;  // the facing a push (the Split Head's) turns him to
    std::int32_t flags;
    std::int16_t quick_turn_heading;
    std::int16_t aim_pitch;
    std::int16_t heading;
    std::int16_t move_speed;
};

constexpr std::size_t kCharacterVarBytes = 64;

// The behaviour variables of a character: one store per record whose layout is data, not code. Every type but Harry
// names its values in gamedata/chara/<Name>/state.json (byte offsets; a mod adds fields there), Harry's are HarryVars.
// The types share the store's words on purpose: the collision reset clears its first words on every record, and the
// base API reads Harry's names (player_ground_height, ...) on NPCs and the NPC names on story characters, so one
// store serves every view of it.
union CharacterVars {
    std::uint8_t bytes[kCharacterVarBytes];
    HarryVars harry;
};
static_assert(sizeof(HarryVars) == kCharacterVarBytes, "Harry's values fill his store");

// One character slot.
struct CharacterRecord {
    ModelRecord model;  // the player: the lower body
    Vec3 position;
    Angles rotation;
    std::int16_t angle_to_target;
    Angles rotation_speed;
    std::int16_t angle_to_target_speed;
    std::int32_t fall_speed;
    std::int32_t move_speed;
    std::int16_t heading;
    std::int16_t flags;  // CharacterFlag bits
    // NPCs: the row of the map's spawn table it came from; the player: the NPC index that attacks him.
    std::int32_t partner;
    std::int32_t attack_received;  // packed weapon attack that hit it
    AttackRecord attack;
    std::int32_t health;
    DamageRecord damage;
    std::uint16_t death_timer;
    std::int16_t timer;
    CollisionRecord collision;
    CharacterVars vars;
    // The type's variant (the Puppet Nurse's stats row) plus one; 0: none chosen.
    std::uint8_t type_variant;
};

// The player's upper body: its own model and clip, the bones its animation leaves alone, Harry's states.
struct UpperBodyRecord {
    ModelRecord model;
    std::int32_t disabled_bones;
    std::int32_t state;
    std::int32_t upper_state;
    std::int32_t lower_state;
    std::int32_t last_used_item;
};

// A bone transform of the game is a frame of the view system's hierarchy: rotation (Q12) and translation (geometry
// units), the world matrix cache (world_current 0: recompute it) and the links of the hierarchy.
using BoneMatrix = sh::camera::Matrix;
using BoneCoord = sh::camera::FrameRecord;

constexpr int kNpcSlotCount = 120;
constexpr int kPlayerBoneCount = 18;
constexpr int kPlayerSpareBones = 5;
constexpr int kNpcBonePoolSize = 10 * kNpcSlotCount;

// All characters of the session: slot 0 is the player, slots 1..120 the NPCs.
struct CharacterSlots {
    CharacterRecord player;
    UpperBodyRecord player_upper;
    CharacterRecord npcs[kNpcSlotCount];
    BoneCoord player_bones[kPlayerBoneCount];
    BoneCoord player_spare_bones[kPlayerSpareBones];
    BoneCoord npc_bone_pool[kNpcBonePoolSize];

    // The record of `slot` (0 the player, 1.. the NPCs), nullptr out of range.
    CharacterRecord* record(int slot) {
        if (slot == 0) {
            return &player;
        }
        return slot >= 1 && slot <= kNpcSlotCount ? &npcs[slot - 1] : nullptr;
    }
    const CharacterRecord* record(int slot) const { return const_cast<CharacterSlots*>(this)->record(slot); }

    // The slot of a record of this block, -1 for anything else.
    int slot_of(const void* record) const {
        if (record == &player) {
            return 0;
        }
        const auto* r = static_cast<const CharacterRecord*>(record);
        if (r >= &npcs[0] && r < &npcs[kNpcSlotCount]) {
            return static_cast<int>(r - npcs) + 1;
        }
        return -1;
    }
};

}  // namespace sh::entities
