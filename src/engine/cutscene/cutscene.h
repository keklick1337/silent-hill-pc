// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Cutscenes as data: convert/cutscenes/**/<name>.dms.json (sh-cutscene/1).
//
// A cutscene is a timeline at 30 frames per second with one camera track and any number of named actor tracks
// (characters, and markers such as the flashlight "LIGHT" and its aim "L_INT"). Every track stores keys; hold
// ranges let one key cover a run of frames, so a track usually has fewer keys than the timeline has frames.
// Segments cut the timeline into shots: the last frame of a shot is not blended into the next shot's first frame.
//
// All values stay in the integer units the game logic uses, and sampling reproduces the original arithmetic
// exactly (same truncation, same angle wrapping), so a sampled pose is bit-identical to the original player:
//   positions  Q8 world units relative to `origin` in the file (256 = 1 m), Q12 in the sampled pose (4096 = 1 m);
//              axes as in the game: +X right, +Y down, +Z forward;
//   angles     Q12 turns (4096 = 360 degrees), wrapped to [-2048, 2047];
//   time       Q12 frames (4096 = one frame at 30 fps).
#pragma once

#include "formats/common/status.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::cutscene {

inline constexpr int kFramesPerSecond = 30;
inline constexpr std::int32_t kTimeOne = 4096;  // one frame in Q12 time

using Vec3s = std::array<std::int16_t, 3>;
using Vec3i = std::array<std::int32_t, 3>;

// Frames first..last (inclusive) show key `key` of the track.
struct HoldRange {
    std::int16_t first = 0;
    std::int16_t last = 0;
    std::int16_t key = 0;
};

struct ActorKey {
    Vec3s position{};  // Q8, relative to the cutscene origin
    Vec3s rotation{};  // Q12 angles
};

struct CameraKey {
    Vec3s eye{};                   // camera position, Q8 relative to the origin
    Vec3s target{};                // look-at point, Q8 relative to the origin
    std::int16_t roll = 0;         // Q12 angle; interpolated like the others, unused by the game
    std::int16_t projection = 0;   // projection distance (screen scale): larger is narrower
};

// Track names are four bytes, the way the game compares them ("DAHLIA" finds "DAHL").
using TrackName = std::array<char, 4>;

template <typename Key>
struct Track {
    TrackName name{};
    std::vector<HoldRange> holds;  // ascending by frame
    std::vector<Key> keys;

    // The key shown at timeline frame `frame`, clamped into the key list (0 when the track has no keys).
    std::size_t key_for_frame(std::int32_t frame) const;
};

using ActorTrack = Track<ActorKey>;
using CameraTrack = Track<CameraKey>;

struct Segment {
    std::int16_t first = 0;  // first frame of the shot
    std::int16_t count = 0;  // frames in the shot
};

struct ActorPose {
    Vec3i position{};  // Q12 world position
    Vec3s rotation{};  // Q12 angles
};

struct CameraPose {
    Vec3i eye{};       // Q12 world position
    Vec3i target{};    // Q12 world look-at point
    std::int16_t roll = 0;
    std::int16_t projection = 0;
};

// The two keys a time blends and the blend weight (Q12; may exceed 4096 on a shot's last frame, where the
// motion of the previous frame is carried on instead of blending into the next shot).
struct Blend {
    std::size_t from = 0;
    std::size_t to = 0;
    std::int32_t alpha = 0;
};

class Cutscene {
public:
    std::string source;  // where it was loaded from, for messages
    Vec3i origin{};      // Q8 world position the track positions are relative to
    std::vector<Segment> segments;
    CameraTrack camera;
    std::vector<ActorTrack> actors;

    // Length of the timeline in frames (the end of the last segment).
    std::int32_t frame_count() const;

    // Index of the first actor whose name matches the first four characters of `name`.
    std::optional<std::size_t> find_actor(std::string_view name) const;

    // Index of the shot (segment) a time falls in, -1 outside every shot.
    int shot_at(std::int32_t time_q12) const;

    ActorPose actor_pose(std::size_t actor, std::int32_t time_q12) const;
    CameraPose camera_pose(std::int32_t time_q12) const;

    template <typename Key>
    Blend blend_at(const Track<Key>& track, std::int32_t time_q12) const;
};

// Four-character comparison with the rules of the game's lookup: equal up to the first NUL of `name`.
bool track_name_matches(const TrackName& track, std::string_view name);
std::string track_name_text(const TrackName& name);

// sh-cutscene/1 text -> Cutscene. Reads only what playback needs (origin, segments, camera, characters);
// the layout fields the converter keeps for a byte-exact rebuild (offsets, fill, size) are optional.
Result<Cutscene> parse_cutscene_json(std::string_view text, std::string source = {});

}  // namespace sh::cutscene
