// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Skeletons and keyframes of the entity module, read from the imported animations
// (gamedata/convert/anim/**/*.anim.json).
//
// A skeleton is a bone table: parent, the translation and rotation tracks a bone reads (or none: the bone keeps
// its rest translation / the identity rotation) and the rest translation. A keyframe stores one value per track:
// translations as 8-bit values scaled by 2^scale_log2 (geometry units), rotations as 3x3 8-bit matrices scaled by
// 32 (Q12). A character's keyframes are numbered globally (KeyframeLayout): its own file brings 0..n-1, and
// keyframe-only files in the same layout (Harry's map and weapon animations) are placed behind them at the index
// the character's clip tables expect.
//
// sample_pose() interpolates two keyframes into a pose with the integer rules the game's results were made with;

#pragma once

#include "engine/entities/fixed_transform.h"
#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sh::convert::anim {
class AnmFile;
}

namespace sh::entities {

inline constexpr int kNoTrack = -1;

struct SkeletonBone {
    int parent = -1;                 // bone index; -1 or itself: no parent
    int rotation_track = kNoTrack;   // index of the rotation value in a keyframe
    int translation_track = kNoTrack;
    std::array<std::int8_t, 3> rest{0, 0, 0};  // rest translation, scaled like a track value
};

struct Skeleton {
    std::vector<SkeletonBone> bones;
    int translation_tracks = 0;
    int rotation_tracks = 0;
    int scale_log2 = 0;              // translation value << scale_log2 = geometry units
    int root_drop = 0;               // subtracted from Y of translation track 0 (the root's height offset)
    std::uint32_t animated_bones = 0;  // bones the animation drives by default (bone i: bit i mod 32)

    std::size_t bone_count() const { return bones.size(); }
    // Bytes of one keyframe: 3 per translation track, then 9 per rotation track.
    std::size_t keyframe_stride() const {
        return static_cast<std::size_t>(translation_tracks) * 3 + static_cast<std::size_t>(rotation_tracks) * 9;
    }
    bool operator==(const Skeleton&) const = default;
};

// Values of one keyframe: `stride` signed bytes (translations first).
using KeyframeValues = std::span<const std::int8_t>;

// The keyframes of one file.
struct KeyframeBlock {
    int count = 0;
    std::vector<std::int8_t> values;  // count * stride
    std::string source;               // game path of the file ("ANIM/HB_WEP1.ANM"), for messages
};

// One file of the import: a character's skeleton with its keyframes, or a keyframe-only file in the layout of
// another file's skeleton (`keyframe_only`, `base` names that file).
class AnimationSet {
public:
    // `base`: the file a keyframe-only `file` extends ("ANIM/HB_BASE.ANM").
    static Result<AnimationSet> from_anm(const sh::convert::anim::AnmFile& file, std::string game_path,
                                         std::string base = {});
    // <path> is a sh-anim/1 JSON file (anim_json.h).
    static Result<AnimationSet> load_json(const std::filesystem::path& path);
    static Result<AnimationSet> parse_json(std::string_view text);

    const std::string& game_path() const { return game_path_; }
    bool keyframe_only() const { return keyframe_only_; }
    const std::string& base() const { return base_; }
    const Skeleton& skeleton() const { return skeleton_; }
    const KeyframeBlock& keyframes() const { return block_; }
    // Keyframe `index` of this file (0-based), or an empty span past its end.
    KeyframeValues keyframe(int index) const;

    // Keyframe count the file's header declares (a keyframe-only file: its base's).
    int declared_keyframes() const { return declared_keyframes_; }

    // Layout of the source file, for code that addresses the file in memory.
    std::size_t source_data_offset() const { return data_offset_; }
    std::size_t source_size() const { return source_size_; }

private:
    std::string game_path_;
    std::string base_;
    bool keyframe_only_ = false;
    Skeleton skeleton_;
    KeyframeBlock block_;
    int declared_keyframes_ = 0;
    std::size_t data_offset_ = 0;
    std::size_t source_size_ = 0;
};

// The global keyframe numbering of a character: files placed at a first index each (later placements over the
// same indices replace earlier ones).
class KeyframeLayout {
public:
    void clear() { placed_.clear(); }
    void place(std::shared_ptr<const AnimationSet> set, int first_index);
    // Removes every placement of `set`.
    void remove(const AnimationSet* set);
    KeyframeValues keyframe(int index) const;
    // The file and its local index of keyframe `index`; nullptr when nothing is placed there.
    const AnimationSet* find(int index, int* local = nullptr) const;
    bool empty() const { return placed_.empty(); }

private:
    struct Placed {
        std::shared_ptr<const AnimationSet> set;
        int first = 0;
    };
    std::vector<Placed> placed_;
};

// The pose of a skeleton: one local transform per bone (bone 0 is the character's own transform).
using Pose = std::vector<BoneTransform>;

// Rest pose (the game's bone setup when an animation file is attached): bone 0 identity; every other bone
// without a translation track gets its rest translation, without a rotation track the identity rotation. Values
// a track will write are left as they are.
void apply_rest_pose(const Skeleton& skeleton, std::span<BoneTransform> pose);

struct SampleRule {
    std::uint32_t bones = 0;       // which bones are written: bone i reads bit i mod 32 (bone 0 is never written)
    int copy_translation_from = -1;  // after sampling, this bone's translation is copied to `..._to` (Harry's
    int copy_translation_to = -1;    // hips -> torso); -1: no copy
};

// Writes bones 1..n-1 that `rule.bones` selects: translation from track values (frame0 + (frame1 - frame0) *
// alpha, alpha Q12) where the bone has a translation track, rotation where it has a rotation track. `written`
// (optional, one entry per bone) is set to 1 for every bone written. frame0/frame1 must be keyframe_stride()
// long. Returns false (pose untouched) when they are not.
bool sample_pose(const Skeleton& skeleton, KeyframeValues frame0, KeyframeValues frame1, std::int32_t alpha,
                 const SampleRule& rule, std::span<BoneTransform> pose, std::span<std::uint8_t> written = {});

// The same interpolation without integer truncation, for renderers that draw between the game's steps.
struct PreciseBone {
    bool has_rotation = false;
    bool has_translation = false;
    std::array<double, 9> rotation{};
    std::array<double, 3> translation{};
};
bool sample_pose_precise(const Skeleton& skeleton, KeyframeValues frame0, KeyframeValues frame1, std::int32_t alpha,
                         int bone, PreciseBone& out);

// World transforms: world[0] = pose[0] (the character), world[i] = world[parent] * pose[i] (compose()), parents
// resolved first. A bone whose parent is out of range, itself or part of a cycle hangs from bone 0.
void compute_world(const Skeleton& skeleton, std::span<const BoneTransform> pose, std::span<BoneTransform> world);

}  // namespace sh::entities
