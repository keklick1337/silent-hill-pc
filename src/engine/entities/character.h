// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A character in the world: its slot record (transform,
// motion, health, damage, flags, control state, animation state, collision shape, attack, timers, behaviour
// variables), its skeleton and animation, and its data (sh::chara::CharacterDef: sounds, params, named clips).
// Behaviour (Harry's controls, enemy AI) is not here: it lives in Lua type by type and drives a Character through
// this class and the sh.entity.* API.
//
// Storage: a Character bound by EntityWorld reads and writes its slot of sh::game::Session
// (session().system.characters) and the bone transforms of its skeleton directly; nothing is copied. An unbound
// Character (tests, tools) owns a record of its own.

#pragma once

#include "engine/chara/behavior_state.h"
#include "engine/entities/anim_player.h"
#include "engine/entities/animation_set.h"
#include "engine/entities/character_record.h"
#include "engine/entities/fixed_transform.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sh::chara {
struct CharacterDef;
}

namespace sh::entities {

// A reference to the character in a slot that stays valid only while that character lives there (EntityWorld
// bumps the slot's generation when it creates or removes one).
struct CharacterRef {
    int slot = -1;
    std::uint32_t generation = 0;
    bool valid() const { return slot >= 0; }
    bool operator==(const CharacterRef&) const = default;
};

// Where the bones of a character come from this tick.
enum class PoseSource : std::uint8_t {
    Animation,  // the clip table entry in anim_state() (the default)
    External,   // a cutscene or script sets bones with set_bone_local(); the animation does not overwrite them
};

class Character {
public:
    Character() = default;
    // An unbound character with a record of its own.
    Character(int slot, int chara_id, const sh::chara::CharacterDef* def);

    // Binds the character to `record` (a slot of the session); the bones and animation stay unbound until
    // bind_bones().
    void bind(int slot, CharacterRecord* record, const sh::chara::CharacterDef* def);
    // The bone transforms the game poses for this character and the animation file they come from.
    void bind_bones(BoneCoord* bones, int count, std::shared_ptr<const AnimationSet> set);
    bool bound() const { return bound_ != nullptr; }

    int slot() const { return slot_; }  // 0 = the player, 1.. = the NPC slots of the map
    int chara_id() const { return record().model.chara_id; }  // characters.json "id"
    const sh::chara::CharacterDef* def() const { return def_; }

    // Data of the character (CharacterDef).
    int sound(std::string_view slot, int fallback = -1) const;
    double param(std::string_view name, double fallback) const;
    std::int32_t param_q12(std::string_view name, std::int32_t fallback) const;
    // Original animation index of a named clip ("walk_forward"), -1 when the data does not name it.
    int clip_index(std::string_view name) const;

    // The slot record itself (for the fields without a named accessor).
    CharacterRecord& record() { return bound_ != nullptr ? *bound_ : own_; }
    const CharacterRecord& record() const { return bound_ != nullptr ? *bound_ : own_; }

    // Transform and motion.
    const Vec3& position() const { return record().position; }
    const Angles& rotation() const { return record().rotation; }
    void set_position(const Vec3& p) { record().position = p; }
    void set_rotation(const Angles& r) { record().rotation = r; }
    void set_transform(const Vec3& p, const Angles& r) { set_position(p); set_rotation(r); }
    const Angles& rotation_speed() const { return record().rotation_speed; }
    void set_rotation_speed(const Angles& r) { record().rotation_speed = r; }
    std::int16_t angle_to_target() const { return record().angle_to_target; }
    std::int32_t move_speed() const { return record().move_speed; }
    void set_move_speed(std::int32_t q12) { record().move_speed = q12; }
    std::int16_t heading() const { return record().heading; }
    void set_heading(std::int16_t angle) { record().heading = angle; }
    std::int32_t fall_speed() const { return record().fall_speed; }
    void set_fall_speed(std::int32_t q12) { record().fall_speed = q12; }

    // Life.
    std::int32_t health() const { return record().health; }
    void set_health(std::int32_t q12) { record().health = q12; }
    DamageRecord& damage() { return record().damage; }
    const DamageRecord& damage() const { return record().damage; }
    void add_damage(std::int32_t amount, const Vec3& push);
    int attack_received() const { return record().attack_received; }
    void set_attack_received(int attack) { record().attack_received = attack; }
    std::uint16_t death_timer() const { return record().death_timer; }
    void set_death_timer(std::uint16_t t) { record().death_timer = t; }
    std::int16_t timer() const { return record().timer; }
    void set_timer(std::int16_t t) { record().timer = t; }

    // Flags (CharacterFlag bits).
    std::uint32_t flags() const { return static_cast<std::uint16_t>(record().flags); }
    void set_flags(std::uint32_t flags) { record().flags = static_cast<std::int16_t>(flags); }
    bool has_flag(CharacterFlag f) const { return (flags() & f) != 0; }
    void set_flag(CharacterFlag f, bool on) { set_flags(on ? (flags() | f) : (flags() & ~static_cast<std::uint32_t>(f))); }

    // Control state of the type's behaviour.
    std::int32_t control_state() const { return record().model.control_state; }
    std::int32_t control_step() const { return record().model.control_step; }
    void set_control(std::int32_t state, std::int32_t step);

    // Collision shape and attack.
    CollisionRecord& collision() { return record().collision; }
    const CollisionRecord& collision() const { return record().collision; }
    AttackRecord& attack() { return record().attack; }
    const AttackRecord& attack() const { return record().attack; }

    // Links: an NPC's row of the map's spawn table; the player's attacker (an NPC slot), as references.
    int spawn_index() const { return record().partner; }
    CharacterRef attacker() const;
    void set_attacker(const CharacterRef& npc) { record().partner = npc.valid() ? npc.slot - 1 : -1; }

    // Animation: the clip table (entry = status), its state, and the attached keyframes.
    // The character's file (skeleton + keyframes from 0); more keyframe files go into keyframes().
    void attach_animation(std::shared_ptr<const AnimationSet> set);
    const AnimationSet* animation() const { return animation_.get(); }
    KeyframeLayout& keyframes() { return layout_; }
    const KeyframeLayout& keyframes() const { return layout_; }
    std::vector<ClipEntry>& clip_table() { return clips_; }
    const std::vector<ClipEntry>& clip_table() const { return clips_; }
    AnimState anim_state() const;
    // Status, time, keyframe and alpha, and the advance/posed flags (other flag bits are kept).
    void set_anim_state(const AnimState& s);
    // Starts clip `index` (original anim index): blend phase, or straight into playback with `blend` false.
    void play(int index, bool blend = true);
    bool play(std::string_view clip, bool blend = true);
    int playing_clip() const { return record().model.anim.status >> 1; }
    bool blending() const { return (record().model.anim.status & 1) == 0; }
    // One tick of the clip table at `delta_time` (Q12 seconds): advances anim_state() and, with PoseSource
    // Animation and attached keyframes, samples the pose. Returns the step (which keyframes, linked or not).
    StepResult tick(std::int32_t delta_time);

    // Pose (local, bone 0 = the character itself) and world transforms of the bones. With bound bones the pose
    // is the game's bone transforms.
    PoseSource pose_source() const { return pose_source_; }
    void set_pose_source(PoseSource s) { pose_source_ = s; }
    // The unbound pose (bones of an unbound character).
    Pose& pose() { return pose_; }
    const Pose& pose() const { return pose_; }
    int bone_count() const;
    BoneTransform bone_local(int bone) const;
    void set_bone_local(int bone, const BoneTransform& t);
    void update_world();  // world() from the pose (compute_world)
    const std::vector<BoneTransform>& world() const { return world_; }
    // World position of a bone in geometry units (256 = 1 m) from the last update_world(); {0,0,0} out of range.
    std::array<std::int32_t, 3> bone_world_position(int bone) const;

    // The type's behaviour variables (`vars`): named fields of the record's variable block, laid
    // out by gamedata/chara/<Name>/state.json.
    sh::chara::BehaviorVars& vars() { return vars_; }
    const sh::chara::BehaviorVars& vars() const { return vars_; }

private:
    void pose_into(Pose& out) const;
    void store_pose(const Pose& pose);

    int slot_ = -1;
    const sh::chara::CharacterDef* def_ = nullptr;
    CharacterRecord* bound_ = nullptr;
    CharacterRecord own_{};
    BoneCoord* bones_ = nullptr;
    int bones_count_ = 0;
    std::shared_ptr<const AnimationSet> animation_;
    KeyframeLayout layout_;
    std::vector<ClipEntry> clips_;
    PoseSource pose_source_ = PoseSource::Animation;
    Pose pose_;
    std::vector<BoneTransform> world_;
    sh::chara::BehaviorVars vars_;
};

// The game's bone transform <-> BoneTransform.
BoneTransform to_bone_transform(const BoneMatrix& m);
void store_bone_transform(const BoneTransform& t, BoneMatrix& m);

}  // namespace sh::entities
