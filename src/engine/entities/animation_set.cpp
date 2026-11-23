// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/entities/animation_set.h"

#include "formats/convert/anim/anim_json.h"
#include "formats/convert/anim/anm_file.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace sh::entities {

Result<AnimationSet> AnimationSet::from_anm(const sh::convert::anim::AnmFile& file, std::string game_path,
                                            std::string base) {
    const auto& h = file.header();
    AnimationSet set;
    set.game_path_ = std::move(game_path);
    set.base_ = std::move(base);
    set.keyframe_only_ = file.extension();
    set.data_offset_ = file.extension() ? 0 : h.data_offset;
    set.source_size_ = h.file_size;
    set.declared_keyframes_ = h.keyframe_count;
    Skeleton& sk = set.skeleton_;
    sk.translation_tracks = h.translation_slots;
    sk.rotation_tracks = h.rotation_slots;
    sk.scale_log2 = h.scale_log2;
    sk.root_drop = h.root_y_offset;
    sk.animated_bones = h.active_bones;
    sk.bones.reserve(file.bones().size());
    for (const auto& b : file.bones()) {
        SkeletonBone bone;
        bone.parent = b.parent;
        bone.rotation_track = b.rotation;
        bone.translation_track = b.translation;
        for (std::size_t c = 0; c < 3; ++c) {
            bone.rest[c] = static_cast<std::int8_t>(b.bind[c]);
        }
        sk.bones.push_back(bone);
    }
    const std::size_t stride = sk.keyframe_stride();
    if (stride != h.keyframe_size) {
        return Result<AnimationSet>::fail(set.game_path_ + ": keyframe size " + std::to_string(h.keyframe_size) +
                                          " is not the tracks' " + std::to_string(stride));
    }
    KeyframeBlock& block = set.block_;
    block.source = set.game_path_;
    block.count = static_cast<int>(file.keyframes().size());
    block.values.reserve(static_cast<std::size_t>(block.count) * stride);
    for (const auto& k : file.keyframes()) {
        for (const auto& t : k.translations) {
            block.values.insert(block.values.end(), t.begin(), t.end());
        }
        for (const auto& r : k.rotations) {
            block.values.insert(block.values.end(), r.begin(), r.end());
        }
    }
    if (set.keyframe_only_) {
        set.source_size_ = static_cast<std::size_t>(block.count) * stride;  // the header is the base file's
    }
    return Result<AnimationSet>::ok(std::move(set));
}

Result<AnimationSet> AnimationSet::parse_json(std::string_view text) {
    auto file = sh::convert::anim::anm_from_json(text);
    if (!file) {
        return Result<AnimationSet>::fail(file.error());
    }
    // The game path and the base are fields of the JSON; take them without a second parser.
    auto field = [&](std::string_view key) -> std::string {
        const std::string quoted = "\"" + std::string(key) + "\"";
        std::size_t at = text.find(quoted);
        if (at == std::string_view::npos) {
            return {};
        }
        at = text.find('"', text.find(':', at + quoted.size()) + 1);
        const std::size_t end = at == std::string_view::npos ? at : text.find('"', at + 1);
        return end == std::string_view::npos ? std::string() : std::string(text.substr(at + 1, end - at - 1));
    };
    auto set = from_anm(file.value(), field("path"));
    if (set) {
        set.value().base_ = field("base");
    }
    return set;
}

Result<AnimationSet> AnimationSet::load_json(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<AnimationSet>::fail(path.string() + ": cannot open");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    auto set = parse_json(ss.str());
    if (!set) {
        return Result<AnimationSet>::fail(path.string() + ": " + set.error());
    }
    return set;
}

KeyframeValues AnimationSet::keyframe(int index) const {
    if (index < 0 || index >= block_.count) {
        return {};
    }
    const std::size_t stride = skeleton_.keyframe_stride();
    return KeyframeValues(block_.values.data() + static_cast<std::size_t>(index) * stride, stride);
}

void KeyframeLayout::place(std::shared_ptr<const AnimationSet> set, int first_index) {
    if (set != nullptr) {
        placed_.push_back(Placed{std::move(set), first_index});
    }
}

void KeyframeLayout::remove(const AnimationSet* set) {
    std::erase_if(placed_, [&](const Placed& p) { return p.set.get() == set; });
}

const AnimationSet* KeyframeLayout::find(int index, int* local) const {
    for (auto it = placed_.rbegin(); it != placed_.rend(); ++it) {
        const int at = index - it->first;
        if (at >= 0 && at < it->set->keyframes().count) {
            if (local != nullptr) {
                *local = at;
            }
            return it->set.get();
        }
    }
    return nullptr;
}

KeyframeValues KeyframeLayout::keyframe(int index) const {
    int local = 0;
    const AnimationSet* set = find(index, &local);
    return set == nullptr ? KeyframeValues{} : set->keyframe(local);
}

void apply_rest_pose(const Skeleton& skeleton, std::span<BoneTransform> pose) {
    if (pose.empty()) {
        return;
    }
    pose[0] = kIdentityTransform;
    const std::size_t n = std::min(pose.size(), skeleton.bones.size());
    for (std::size_t i = 1; i < n; ++i) {
        const SkeletonBone& b = skeleton.bones[i];
        if (b.translation_track < 0) {
            for (std::size_t c = 0; c < 3; ++c) {
                pose[i].translation[c] = std::int32_t{b.rest[c]} * (1 << skeleton.scale_log2);
            }
        }
        if (b.rotation_track < 0) {
            pose[i].rotation = kIdentityTransform.rotation;
        }
    }
}

bool sample_pose(const Skeleton& skeleton, KeyframeValues frame0, KeyframeValues frame1, std::int32_t alpha,
                 const SampleRule& rule, std::span<BoneTransform> pose, std::span<std::uint8_t> written) {
    const std::size_t stride = skeleton.keyframe_stride();
    if (stride == 0 || frame0.size() != stride || frame1.size() != stride) {
        return false;
    }
    const std::size_t rotations_at = static_cast<std::size_t>(skeleton.translation_tracks) * 3;
    const int scale = skeleton.scale_log2;
    const std::size_t n = std::min(pose.size(), skeleton.bones.size());
    for (std::size_t i = 1; i < n; ++i) {
        // Skeletons have up to 53 bones and the mask 32 bits: bone i reads bit i mod 32, as the game's shift
        // (MIPS sllv, x86 shl) does.
        if ((rule.bones & (1u << (i & 31))) == 0) {
            continue;
        }
        const SkeletonBone& b = skeleton.bones[i];
        BoneTransform& out = pose[i];
        if (!written.empty() && i < written.size()) {
            written[i] = 1;
        }
        if (b.translation_track >= 0 && b.translation_track < skeleton.translation_tracks) {
            const std::size_t at = static_cast<std::size_t>(b.translation_track) * 3;
            for (std::size_t c = 0; c < 3; ++c) {
                const std::int32_t a = frame0[at + c];
                const std::int32_t z = frame1[at + c];
                out.translation[c] = a * (1 << scale) + (((z - a) * alpha) >> (12 - scale));
            }
            if (b.translation_track == 0) {
                out.translation[1] -= skeleton.root_drop;
            }
        }
        if (b.rotation_track >= 0 && b.rotation_track < skeleton.rotation_tracks) {
            const std::size_t at = rotations_at + static_cast<std::size_t>(b.rotation_track) * 9;
            for (std::size_t c = 0; c < 9; ++c) {
                const std::int32_t a = frame0[at + c];
                const std::int32_t z = frame1[at + c];
                // Stored in 16 bits like the game keeps a matrix entry.
                out.rotation[c] = static_cast<std::int16_t>(a * 32 + (((z - a) * alpha) >> 7));
            }
        }
    }
    const int from = rule.copy_translation_from;
    const int to = rule.copy_translation_to;
    if (from >= 0 && to >= 0 && static_cast<std::size_t>(from) < pose.size() && static_cast<std::size_t>(to) < pose.size()) {
        pose[static_cast<std::size_t>(to)].translation = pose[static_cast<std::size_t>(from)].translation;
    }
    return true;
}

bool sample_pose_precise(const Skeleton& skeleton, KeyframeValues frame0, KeyframeValues frame1, std::int32_t alpha,
                         int bone, PreciseBone& out) {
    const std::size_t stride = skeleton.keyframe_stride();
    if (stride == 0 || frame0.size() != stride || frame1.size() != stride || bone < 0 ||
        static_cast<std::size_t>(bone) >= skeleton.bones.size()) {
        return false;
    }
    const SkeletonBone& b = skeleton.bones[static_cast<std::size_t>(bone)];
    const int scale = skeleton.scale_log2;
    out = PreciseBone{};
    if (b.translation_track >= 0 && b.translation_track < skeleton.translation_tracks) {
        out.has_translation = true;
        const std::size_t at = static_cast<std::size_t>(b.translation_track) * 3;
        for (std::size_t c = 0; c < 3; ++c) {
            const std::int32_t a = frame0[at + c];
            const std::int32_t z = frame1[at + c];
            out.translation[c] = static_cast<double>(a * (1 << scale)) +
                                 static_cast<double>((z - a) * alpha) / static_cast<double>(1 << (12 - scale));
        }
        if (b.translation_track == 0) {
            out.translation[1] -= skeleton.root_drop;
        }
    }
    if (b.rotation_track >= 0 && b.rotation_track < skeleton.rotation_tracks) {
        out.has_rotation = true;
        const std::size_t at = static_cast<std::size_t>(skeleton.translation_tracks) * 3 +
                               static_cast<std::size_t>(b.rotation_track) * 9;
        for (std::size_t c = 0; c < 9; ++c) {
            const std::int32_t a = frame0[at + c];
            const std::int32_t z = frame1[at + c];
            out.rotation[c] = static_cast<double>(a * 32) + static_cast<double>((z - a) * alpha) / 128.0;
        }
    }
    return true;
}

void compute_world(const Skeleton& skeleton, std::span<const BoneTransform> pose, std::span<BoneTransform> world) {
    const std::size_t n = std::min(pose.size(), world.size());
    if (n == 0) {
        return;
    }
    std::vector<std::uint8_t> state(n, 0);  // 0 unvisited, 1 in progress, 2 done
    world[0] = pose[0];
    state[0] = 2;
    // Parents may come after their children in the table; resolve along the chain, depth bounded by n.
    std::vector<std::size_t> chain;
    for (std::size_t i = 1; i < n; ++i) {
        std::size_t at = i;
        chain.clear();
        while (state[at] != 2) {
            state[at] = 1;
            chain.push_back(at);
            const int p = at < skeleton.bones.size() ? skeleton.bones[at].parent : 0;
            const std::size_t parent = (p < 0 || static_cast<std::size_t>(p) >= n || state[static_cast<std::size_t>(p)] == 1)
                                           ? 0
                                           : static_cast<std::size_t>(p);
            if (state[parent] == 2) {
                break;
            }
            at = parent;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const std::size_t b = *it;
            const int p = b < skeleton.bones.size() ? skeleton.bones[b].parent : 0;
            std::size_t parent = (p < 0 || static_cast<std::size_t>(p) >= n) ? 0 : static_cast<std::size_t>(p);
            if (state[parent] != 2) {
                parent = 0;  // a cycle
            }
            world[b] = compose(world[parent], pose[b]);
            state[b] = 2;
        }
    }
}

}  // namespace sh::entities
