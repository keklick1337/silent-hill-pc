// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/anim_bank.h"

#include "engine/entities/anim_player_impl.h"
#include "engine/entities/character.h"
#include "engine/game_state/session.h"
#include "engine/mods/data_reload.h"

#include <algorithm>

namespace sh::entities {
namespace {

// Harry's rig: the hips' translation is copied to the torso after sampling.
constexpr int kHarryTorso = 1;
constexpr int kHarryHips = 11;

bool is_player_bones(const BoneCoord* bones) {
    return bones == &sh::game::session().system.characters.player_bones[0];
}

// The rule the game applies to a skeleton (which bones, Harry's hips -> torso copy).
SampleRule sample_rule(const Skeleton& skeleton, const BoneCoord* bones) {
    SampleRule rule;
    if (is_player_bones(bones)) {
        rule.bones = ~static_cast<std::uint32_t>(sh::game::session().system.characters.player_upper.disabled_bones);
        rule.copy_translation_from = kHarryHips;
        rule.copy_translation_to = kHarryTorso;
    } else {
        rule.bones = skeleton.animated_bones;
    }
    return rule;
}

}  // namespace

std::shared_ptr<const AnimationSet> AnimBank::load(const std::string& game_path) {
    // `mods reload`: a mod's animation may have changed; sets in use stay alive through their shared pointers.
    if (mods::data_stale(epoch_)) {
        files_.clear();
    }
    if (auto it = files_.find(game_path); it != files_.end()) {
        return it->second;
    }
    std::shared_ptr<const AnimationSet> set = loader_ ? loader_(game_path) : nullptr;
    files_.emplace(game_path, set);
    return set;
}

void AnimBank::forget(std::uintptr_t begin, std::uintptr_t end) {
    std::erase_if(residents_, [&](const Resident& r) { return std::max(r.begin, r.live_from) < end && begin < r.end; });
}

const AnimBank::Resident* AnimBank::resident_at(std::uintptr_t begin) const {
    for (const Resident& r : residents_) {
        if (r.begin == begin && !r.set->keyframe_only()) {
            return &r;
        }
    }
    return nullptr;
}

void AnimBank::file_loaded(const void* data, std::size_t size, const std::string& game_path) {
    const auto begin = reinterpret_cast<std::uintptr_t>(data);
    // A keyframe file longer than the one read over it keeps its tail: the memory behind the new file still holds
    // those keyframes, and a clip started on the last map plays them (Harry rides the lift of map1_s02 into
    // map1_s05, whose file is a quarter as long, still posed at keyframe 1016).
    // Only a file the read overwrites leaves a tail: one whose live part already starts past the new file (the tail
    // of an earlier read) is untouched and stays as it is, so it must not be copied again (it would double with every
    // load: 2^n entries after n map loads).
    std::vector<Resident> tails;
    for (const Resident& r : residents_) {
        if (size > 0 && r.set->keyframe_only() && r.begin <= begin && r.end > begin + size &&
            std::max(r.begin, r.live_from) < begin + size) {
            Resident tail = r;
            tail.live_from = std::max(std::max(r.begin, r.live_from), begin + size);
            tails.push_back(std::move(tail));
        }
    }
    forget(begin, begin + size);
    residents_.insert(residents_.end(), tails.begin(), tails.end());
    if (game_path.size() < 4 || game_path.compare(game_path.size() - 4, 4, ".ANM") != 0) {
        return;
    }
    if (auto set = load(game_path); set != nullptr) {
        residents_.push_back(Resident{begin, begin + std::max<std::size_t>(set->source_size(), 1), set, 0});
    }
}

void AnimBank::memory_copied(void* dest, const void* src, std::size_t size) {
    if (size == 0) {
        return;
    }
    const auto s = reinterpret_cast<std::uintptr_t>(src);
    const auto d = reinterpret_cast<std::uintptr_t>(dest);
    std::vector<Resident> copies;
    for (const Resident& r : residents_) {
        if (r.begin >= s && r.end <= s + size) {
            copies.push_back(Resident{r.begin - s + d, r.end - s + d, r.set, r.live_from != 0 ? r.live_from - s + d : 0});
        }
    }
    forget(d, d + size);
    residents_.insert(residents_.end(), copies.begin(), copies.end());
}

std::shared_ptr<const AnimationSet> AnimBank::file_at(const void* hdr) const {
    const Resident* r = resident_at(reinterpret_cast<std::uintptr_t>(hdr));
    return r != nullptr ? r->set : nullptr;
}

std::shared_ptr<const AnimationSet> AnimBank::posed_by(const void* bones) const {
    for (const auto& [array, set] : posed_) {
        if (array == bones) {
            return set;
        }
    }
    return nullptr;
}

int AnimBank::harry_map_keyframe_first() const {
    for (const Resident& r : residents_) {
        if (is_harry_base(r) && !r.set->keyframe_only()) {
            const std::size_t stride = r.set->skeleton().keyframe_stride();
            const std::uintptr_t data = r.begin + r.set->source_data_offset();
            if (stride == 0 || harry_map_ < data) {
                return -1;
            }
            return static_cast<int>((harry_map_ - data) / stride);
        }
    }
    return -1;
}

int AnimBank::file_keyframes(const std::string& game_path) {
    const std::shared_ptr<const AnimationSet> set = load(game_path);
    return set != nullptr ? set->keyframes().count : -1;
}

int AnimBank::harry_bank_append(const std::string& game_path, int local_first, int count) {
    if (local_first < 0 || count <= 0) {
        return -1;
    }
    const std::shared_ptr<const AnimationSet> set = load(game_path);
    if (set == nullptr || set->keyframes().count <= local_first) {
        return -1;
    }
    if (harry_bank_next() + count > 32767) {
        return -1;
    }
    const int first = harry_bank_next();
    const int last_local = set->keyframes().count - 1;
    for (int i = 0; i < count; ++i) {
        const KeyframeValues k = set->keyframe(std::min(local_first + i, last_local));
        harry_bank_.emplace_back(k.begin(), k.end());
    }
    return first;
}

KeyframeValues AnimBank::keyframe_at(const Resident& base, int index) const {
    const AnimationSet& set = *base.set;
    if (index < 0) {
        return {};
    }
    if (index >= kHarryBankFirst && index - kHarryBankFirst < static_cast<int>(harry_bank_.size()) &&
        is_harry_base(base)) {
        return harry_bank_[static_cast<std::size_t>(index - kHarryBankFirst)];
    }
    if (index < set.keyframes().count) {
        return set.keyframe(index);
    }
    // Past the file's own keyframes: another file the game placed right behind it (Harry's map and weapon files).
    const std::size_t stride = set.skeleton().keyframe_stride();
    const std::uintptr_t at = base.begin + set.source_data_offset() + static_cast<std::uintptr_t>(index) * stride;
    for (const Resident& r : residents_) {
        if (!r.set->keyframe_only() || at < r.begin || at < r.live_from || at >= r.end ||
            r.set->skeleton().keyframe_stride() != stride ||
            (at - r.begin) % stride != 0) {
            continue;
        }
        return r.set->keyframe(static_cast<int>((at - r.begin) / stride));
    }
    return {};
}

bool AnimBank::sample(const void* hdr, BoneCoord* bones, int k0, int k1, int alpha) {
    if (hdr == nullptr || bones == nullptr) {
        return false;
    }
    const Resident* resident = resident_at(reinterpret_cast<std::uintptr_t>(hdr));
    const int k0_asked = k0;
    const int k1_asked = k1;
    KeyframeValues frame0, frame1;
    if (resident != nullptr) {
        const AnimationSet& set = *resident->set;
        // The engine's bounds: skeletons other than Harry's clamp to the file's declared keyframes; negative is 0.
        if (static_cast<int>(set.skeleton().bone_count()) != kPlayerBoneCount) {
            const int declared = set.declared_keyframes();
            if (declared > 0) {
                k0 = k0 >= declared ? declared - 1 : k0;
                k1 = k1 >= declared ? declared - 1 : k1;
            }
        }
        k0 = k0 < 0 ? 0 : k0;
        k1 = k1 < 0 ? 0 : k1;
        frame0 = keyframe_at(*resident, k0);
        frame1 = keyframe_at(*resident, k1);
        if (frame1.empty() && alpha == 0) {
            // A clip that ends on the last keyframe of its file samples it with the one after it at alpha 0. The
            // original reads whatever memory follows the file, and the blend (frame1 - frame0) * 0 drops it, so the
            // pose is exactly frame0.
            frame1 = frame0;
        }
    }
    if (resident == nullptr || frame0.empty() || frame1.empty()) {
        if (unresolved_) {
            unresolved_(hdr, k0_asked, k1_asked);
        }
        return false;
    }

    bool known = false;
    for (auto& [array, set] : posed_) {
        if (array == bones) {
            set = resident->set;
            known = true;
            break;
        }
    }
    if (!known) {
        // The game's bone arrays are few (the player's, the NPC pool, the viewers'): a stale one is dropped.
        if (posed_.size() >= 256) {
            posed_.erase(posed_.begin());
        }
        posed_.emplace_back(bones, resident->set);
    }

    // Bones 1..n-1 start from the game's values, so the ones the rule leaves alone keep them.
    const Skeleton& sk = resident->set->skeleton();
    pose_.resize(sk.bone_count());
    written_.assign(sk.bone_count(), 0);
    for (std::size_t i = 0; i < pose_.size(); ++i) {
        pose_[i] = to_bone_transform(bones[i].local);
    }
    sample_pose(sk, frame0, frame1, alpha, sample_rule(sk, bones), pose_, written_);
    const bool smooth = smoothing_.active && smoothing_.active();
    for (std::size_t i = 1; i < pose_.size(); ++i) {
        if (!written_[i]) {
            continue;
        }
        bones[i].world_current = 0;
        store_bone_transform(pose_[i], bones[i].local);
        if (smooth && smoothing_.bone_local) {
            // The renderer's full-precision twin of the same interpolation.
            PreciseBone precise;
            smoothing_.bone_local(&bones[i], nullptr, nullptr);
            if (sample_pose_precise(sk, frame0, frame1, alpha, static_cast<int>(i), precise)) {
                if (precise.has_translation) {
                    smoothing_.bone_local(&bones[i], nullptr, precise.translation.data());
                }
                if (precise.has_rotation) {
                    smoothing_.bone_local(&bones[i], precise.rotation.data(), nullptr);
                }
            }
        }
    }
    if (is_player_bones(bones)) {
        // The copy also covers a torso the mask left alone, as the game does.
        for (int c = 0; c < 3; ++c) {
            bones[kHarryTorso].local.t[c] = bones[kHarryHips].local.t[c];
        }
        if (smooth && smoothing_.copy_translation) {
            smoothing_.copy_translation(&bones[kHarryTorso], &bones[kHarryHips]);
        }
    }
    return true;
}

StepResult AnimBank::play(ModelRecord& model, const void* hdr, BoneCoord* bones, const ClipEntry& entry,
                          const std::function<std::int32_t()>& timestep) {
    ModelAnimRecord& anim = model.anim;
    AnimState state;
    state.status = anim.status;
    state.flags = static_cast<std::uint8_t>(anim.flags & (kAnimAdvances | kAnimPosed));
    state.time = anim.time;
    state.keyframe = anim.keyframe;
    state.alpha = anim.alpha;
    const StepResult r = step_animation(state, entry, [&] {
        if ((anim.flags & kAnimAdvances) == 0) {
            return 0;
        }
        // A duration function reads the model: show it the time a loop just pulled into range.
        anim.time = state.time;
        return timestep();
    });
    if (r.sample) {
        sample(hdr, bones, r.keyframe0, r.keyframe1, r.alpha);
    }
    anim.status = state.status;
    anim.time = state.time;
    anim.keyframe = static_cast<std::int16_t>(state.keyframe);
    anim.alpha = static_cast<std::int16_t>(state.alpha);
    return r;
}

AnimBank& anim_bank() {
    static AnimBank bank;
    return bank;
}

}  // namespace sh::entities
