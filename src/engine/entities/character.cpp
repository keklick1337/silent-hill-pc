// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/character.h"

#include "engine/chara/chara_library.h"

#include <algorithm>

namespace sh::entities {

namespace {
// The bits of the record's anim flags AnimState carries.
constexpr std::uint16_t kAnimStateFlags = kAnimAdvances | kAnimPosed;
}  // namespace

BoneTransform to_bone_transform(const BoneMatrix& m) {
    BoneTransform t;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            t.rotation[static_cast<std::size_t>(r * 3 + c)] = m.m[r][c];
        }
        t.translation[static_cast<std::size_t>(r)] = m.t[r];
    }
    return t;
}

void store_bone_transform(const BoneTransform& t, BoneMatrix& m) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m.m[r][c] = t.rotation[static_cast<std::size_t>(r * 3 + c)];
        }
        m.t[r] = t.translation[static_cast<std::size_t>(r)];
    }
}

Character::Character(int slot, int chara_id, const sh::chara::CharacterDef* def) : slot_(slot), def_(def) {
    own_.model.chara_id = chara_id;
}

void Character::bind(int slot, CharacterRecord* record, const sh::chara::CharacterDef* def) {
    slot_ = slot;
    bound_ = record;
    def_ = def;
    bones_ = nullptr;
    bones_count_ = 0;
    animation_.reset();
    layout_.clear();
    pose_.clear();
    world_.clear();
}

void Character::bind_bones(BoneCoord* bones, int count, std::shared_ptr<const AnimationSet> set) {
    bones_ = bones;
    bones_count_ = bones != nullptr ? count : 0;
    layout_.clear();
    animation_ = std::move(set);
    if (animation_ != nullptr) {
        layout_.place(animation_, 0);
    }
}

int Character::sound(std::string_view slot, int fallback) const {
    return sh::chara::Character(def_).sfx(slot, fallback);
}

double Character::param(std::string_view name, double fallback) const {
    return sh::chara::Character(def_).param(name, fallback);
}

std::int32_t Character::param_q12(std::string_view name, std::int32_t fallback) const {
    return sh::chara::Character(def_).param_q12(name, fallback);
}

int Character::clip_index(std::string_view name) const { return sh::chara::Character(def_).anim(name, -1); }

void Character::add_damage(std::int32_t amount, const Vec3& push) {
    DamageRecord& d = record().damage;
    d.amount += amount;
    d.push.x += push.x;
    d.push.y += push.y;
    d.push.z += push.z;
}

CharacterRef Character::attacker() const {
    const int npc = record().partner;
    return npc >= 0 && npc < kNpcSlotCount ? CharacterRef{npc + 1, 0} : CharacterRef{};
}

void Character::set_control(std::int32_t state, std::int32_t step) {
    record().model.control_state = static_cast<std::uint8_t>(state);
    record().model.control_step = static_cast<std::uint8_t>(step);
}

AnimState Character::anim_state() const {
    const ModelAnimRecord& a = record().model.anim;
    AnimState s;
    s.status = a.status;
    s.flags = static_cast<std::uint8_t>(a.flags & kAnimStateFlags);
    s.time = a.time;
    s.keyframe = a.keyframe;
    s.alpha = a.alpha;
    return s;
}

void Character::set_anim_state(const AnimState& s) {
    ModelAnimRecord& a = record().model.anim;
    a.status = s.status;
    a.flags = static_cast<std::uint16_t>((a.flags & ~kAnimStateFlags) | (s.flags & kAnimStateFlags));
    a.time = s.time;
    a.keyframe = static_cast<std::int16_t>(s.keyframe);
    a.alpha = static_cast<std::int16_t>(s.alpha);
}

void Character::attach_animation(std::shared_ptr<const AnimationSet> set) {
    layout_.clear();
    animation_ = std::move(set);
    layout_.place(animation_, 0);
    pose_.assign(animation_ != nullptr ? animation_->skeleton().bone_count() : 0, BoneTransform{});
    if (animation_ != nullptr) {
        apply_rest_pose(animation_->skeleton(), pose_);
    }
}

void Character::play(int index, bool blend) {
    if (index < 0 || index > 127) {
        return;
    }
    record().model.anim.status = static_cast<std::uint8_t>((index << 1) | (blend ? 0 : 1));
}

bool Character::play(std::string_view clip, bool blend) {
    const int index = clip_index(clip);
    if (index < 0) {
        return false;
    }
    play(index, blend);
    return true;
}

StepResult Character::tick(std::int32_t delta_time) {
    AnimState anim = anim_state();
    if (anim.status >= clips_.size()) {
        return {};
    }
    const ClipEntry& entry = clips_[anim.status];
    const StepResult r = step_animation_fixed(anim, entry, anim_timestep(entry.speed, delta_time, anim.flags));
    set_anim_state(anim);
    if (r.sample && pose_source_ == PoseSource::Animation && animation_ != nullptr) {
        const Skeleton& sk = animation_->skeleton();
        Pose pose;
        pose_into(pose);
        if (pose.size() < sk.bone_count()) {
            pose.resize(sk.bone_count());
            apply_rest_pose(sk, pose);
        }
        sample_pose(sk, layout_.keyframe(r.keyframe0), layout_.keyframe(r.keyframe1), r.alpha,
                    SampleRule{sk.animated_bones}, pose);
        store_pose(pose);
    }
    return r;
}

int Character::bone_count() const { return bones_ != nullptr ? bones_count_ : static_cast<int>(pose_.size()); }

BoneTransform Character::bone_local(int bone) const {
    if (bone < 0 || bone >= bone_count()) {
        return {};
    }
    if (bones_ != nullptr) {
        return to_bone_transform(bones_[bone].local);
    }
    return pose_[static_cast<std::size_t>(bone)];
}

void Character::set_bone_local(int bone, const BoneTransform& t) {
    if (bone < 0) {
        return;
    }
    if (bones_ != nullptr) {
        if (bone < bones_count_) {
            store_bone_transform(t, bones_[bone].local);
            bones_[bone].world_current = 0;
        }
        return;
    }
    if (static_cast<std::size_t>(bone) >= pose_.size()) {
        pose_.resize(static_cast<std::size_t>(bone) + 1);
    }
    pose_[static_cast<std::size_t>(bone)] = t;
}

void Character::pose_into(Pose& out) const {
    if (bones_ == nullptr) {
        out = pose_;
        return;
    }
    out.resize(static_cast<std::size_t>(bones_count_));
    for (int i = 0; i < bones_count_; ++i) {
        out[static_cast<std::size_t>(i)] = to_bone_transform(bones_[i].local);
    }
}

void Character::store_pose(const Pose& pose) {
    if (bones_ == nullptr) {
        pose_ = pose;
        return;
    }
    const int n = std::min(bones_count_, static_cast<int>(pose.size()));
    for (int i = 0; i < n; ++i) {
        store_bone_transform(pose[static_cast<std::size_t>(i)], bones_[i].local);
        bones_[i].world_current = 0;
    }
}

void Character::update_world() {
    static const Skeleton kNoSkeleton;
    if (bones_ != nullptr) {
        Pose pose;
        pose_into(pose);
        world_.resize(pose.size());
        compute_world(animation_ != nullptr ? animation_->skeleton() : kNoSkeleton, pose, world_);
        return;
    }
    world_.resize(pose_.size());
    compute_world(animation_ != nullptr ? animation_->skeleton() : kNoSkeleton, pose_, world_);
}

std::array<std::int32_t, 3> Character::bone_world_position(int bone) const {
    if (bone < 0 || static_cast<std::size_t>(bone) >= world_.size()) {
        return {0, 0, 0};
    }
    return world_[static_cast<std::size_t>(bone)].translation;
}

}  // namespace sh::entities
