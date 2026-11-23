// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The animation files of the running game and the bones they pose ("Bridge and
// animation data").
//
// The engine never reads the ANM bytes in the game's memory. When the file queue finishes reading an animation
// file, the same file is loaded from the import (convert/anim/ANIM/<NAME>.anim.json, a mod's first, through the
// loader the game installs) and the bank remembers which address range holds it; copies between animation buffers
// move that knowledge along, any other read over the range forgets it. The game still addresses keyframes by
// pointer arithmetic (Harry's map and weapon keyframes sit at fixed buffers behind his base file), so a keyframe
// index is resolved to (file, local keyframe) through those ranges.
//
// Sampling writes the game's bone transforms (BoneCoord arrays: the session's player bones, the NPC pool) with the
// engine's sampler; a clip step advances a model's animation record (ModelRecord) by its clip table entry.

#pragma once

#include "engine/entities/anim_player.h"
#include "engine/entities/animation_set.h"
#include "engine/entities/character_record.h"
#include "engine/mods/data_reload.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sh::entities {

class AnimBank {
public:
    // Loads the import's copy of an animation file by its game path ("ANIM/HB_BASE.ANM"); nullptr when absent.
    using Loader = std::function<std::shared_ptr<const AnimationSet>(const std::string& game_path)>;
    // The renderer's full-precision twin of the sampled bones (smooth models); every call is optional.
    struct Smoothing {
        std::function<bool()> active;
        std::function<void(BoneCoord*, const double* rotation_q12, const double* translation)> bone_local;
        std::function<void(BoneCoord* dst, const BoneCoord* src)> copy_translation;
    };

    // Called when sample() has no data for the keyframes asked (the game logs the first ones).
    using Unresolved = std::function<void(const void* hdr, int k0, int k1)>;

    void set_loader(Loader loader) { loader_ = std::move(loader); }
    void set_unresolved(Unresolved report) { unresolved_ = std::move(report); }
    void set_smoothing(Smoothing smoothing) { smoothing_ = std::move(smoothing); }
    // Where the game keeps Harry's base file and the map's keyframes behind it.
    void set_harry_buffers(const void* base, const void* map) {
        harry_base_ = reinterpret_cast<std::uintptr_t>(base);
        harry_map_ = reinterpret_cast<std::uintptr_t>(map);
    }

    // The import's copy of a file (cached; nullptr when the import has none).
    std::shared_ptr<const AnimationSet> load(const std::string& game_path);

    // The file queue read `game_path` into [data, data + size): a .ANM is now resident there, anything else
    // forgets what lay in the range.
    void file_loaded(const void* data, std::size_t size, const std::string& game_path);
    // A copy between buffers moves the residents of the source range along.
    void memory_copied(void* dest, const void* src, std::size_t size);
    // The file resident at `hdr` (not a keyframe-only file), nullptr when none.
    std::shared_ptr<const AnimationSet> file_at(const void* hdr) const;

    // Harry's reaction clips from another map's file: global keyframe numbers kHarryBankFirst.. of
    // Harry's base file. No file of the game sits there, so the numbers never meet a real one.
    static constexpr int kHarryBankFirst = 4000;
    // The global keyframe number of the first keyframe of the map's file behind the base file, -1 before the base
    // file is resident.
    int harry_map_keyframe_first() const;
    // Copies `count` keyframes of `game_path` from `local_first` to the bank; returns the first global number, -1
    // when the file is missing or too short. Past the file's end the last keyframe repeats.
    int harry_bank_append(const std::string& game_path, int local_first, int count);
    void harry_bank_clear() { harry_bank_.clear(); }
    int harry_bank_next() const { return kHarryBankFirst + static_cast<int>(harry_bank_.size()); }
    // How many keyframes the import has for `game_path`, -1 when it has no such file.
    int file_keyframes(const std::string& game_path);

    // Samples keyframes k0/k1 at `alpha` of the file at `hdr` into `bones`. False when the bank has no data for
    // them (the pose holds; the first ones are logged).
    bool sample(const void* hdr, BoneCoord* bones, int k0, int k1, int alpha);
    // One step of `entry` on the model's animation record; samples into `bones` when the step poses. `timestep`
    // gives the Q12 advance (read after a loop pulled the time into range, with the time written back first).
    StepResult play(ModelRecord& model, const void* hdr, BoneCoord* bones, const ClipEntry& entry,
                    const std::function<std::int32_t()>& timestep);

    // The file whose keyframes last posed the bone array `bones`: its skeleton says which bones the animation
    // moves and which keep a rest offset (the drawer of a model with its own proportions reads it). nullptr when
    // the bank never posed that array.
    std::shared_ptr<const AnimationSet> posed_by(const void* bones) const;

private:
    struct Resident {
        std::uintptr_t begin = 0;
        std::uintptr_t end = 0;
        std::shared_ptr<const AnimationSet> set;
        // Past `begin` when a shorter file was read over the front of this one: only the keyframes from here on
        // are still in memory.
        std::uintptr_t live_from = 0;
    };

    void forget(std::uintptr_t begin, std::uintptr_t end);
    const Resident* resident_at(std::uintptr_t begin) const;
    bool is_harry_base(const Resident& r) const { return r.begin == harry_base_; }
    KeyframeValues keyframe_at(const Resident& base, int index) const;

    Loader loader_;
    Smoothing smoothing_;
    std::uintptr_t harry_base_ = 0;
    std::uintptr_t harry_map_ = 0;
    std::map<std::string, std::shared_ptr<const AnimationSet>, std::less<>> files_;
    std::uint32_t epoch_ = mods::kNeverRead;
    std::vector<Resident> residents_;
    std::vector<std::vector<std::int8_t>> harry_bank_;
    std::vector<BoneTransform> pose_;
    std::vector<std::uint8_t> written_;
    std::vector<std::pair<const void*, std::shared_ptr<const AnimationSet>>> posed_;
    Unresolved unresolved_;
};

// The bank of the running game.
AnimBank& anim_bank();

}  // namespace sh::entities
