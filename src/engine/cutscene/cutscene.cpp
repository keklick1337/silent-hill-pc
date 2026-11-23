// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Sampling of a cutscene timeline (cutscene.h). The arithmetic is the game's: products in 64 bits shifted
// right by 12 (towards minus infinity), sums stored back into 16 bits (wrapping), angles wrapped to 12 bits.
#include "engine/cutscene/cutscene.h"

#include <algorithm>

namespace sh::cutscene {
namespace {

// The way a time is sampled, decided by the shot it falls in.
enum class Phase { Blend, Still, ShotEnd };

std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

std::int16_t blend_s16(std::int16_t from, std::int16_t to, std::int32_t alpha) {
    return static_cast<std::int16_t>(from + mul_q12(to - from, alpha));
}

// Sign-extends the low 12 bits: any angle into [-2048, 2047].
std::int32_t wrap_angle(std::int32_t angle) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(angle) << 20) >> 20;
}

std::int16_t blend_angle(std::int16_t from, std::int16_t to, std::int32_t alpha) {
    return static_cast<std::int16_t>(wrap_angle(mul_q12(wrap_angle(to - from), alpha) + from));
}

// Q8 track value plus the Q8 origin, as a Q12 world value (wraps like the game's 32-bit shift).
std::int32_t to_world(std::int16_t local, std::int32_t origin) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(local + origin) << 4);
}

std::int32_t frame_of(std::int32_t time_q12) { return time_q12 >> 12; }

Phase phase_at(const std::vector<Segment>& segments, std::int32_t frame) {
    for (const Segment& s : segments) {
        if (frame == s.first + s.count - 1) {
            return s.count > 1 ? Phase::ShotEnd : Phase::Still;
        }
    }
    return Phase::Blend;
}

}  // namespace

template <typename Key>
std::size_t Track<Key>::key_for_frame(std::int32_t frame) const {
    std::int32_t key = frame;
    for (const HoldRange& h : holds) {
        if (frame < h.first) {
            break;
        }
        if (frame <= h.last) {
            key = h.key;
            break;
        }
        // Every frame a hold covers before `frame` takes one key instead of one key per frame.
        key -= h.last - h.first;
    }
    const std::int32_t last = static_cast<std::int32_t>(keys.size()) - 1;
    if (key < 0 || last < 0) {
        return 0;
    }
    return static_cast<std::size_t>(std::min(key, last));
}

template struct Track<ActorKey>;
template struct Track<CameraKey>;

std::int32_t Cutscene::frame_count() const {
    std::int32_t frames = 0;
    for (const Segment& s : segments) {
        frames = std::max<std::int32_t>(frames, s.first + s.count);
    }
    return frames;
}

int Cutscene::shot_at(std::int32_t time_q12) const {
    const std::int32_t frame = frame_of(time_q12);
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (frame >= segments[i].first && frame < segments[i].first + segments[i].count) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool track_name_matches(const TrackName& track, std::string_view name) {
    for (std::size_t i = 0; i < track.size(); ++i) {
        const char c = i < name.size() ? name[i] : '\0';
        if (c != track[i]) {
            return false;
        }
        if (c == '\0') {
            return true;
        }
    }
    return true;
}

std::string track_name_text(const TrackName& name) {
    std::string s;
    for (const char c : name) {
        if (c == '\0') {
            break;
        }
        s.push_back(c);
    }
    return s;
}

std::optional<std::size_t> Cutscene::find_actor(std::string_view name) const {
    for (std::size_t i = 0; i < actors.size(); ++i) {
        if (track_name_matches(actors[i].name, name)) {
            return i;
        }
    }
    return std::nullopt;
}

template <typename Key>
Blend Cutscene::blend_at(const Track<Key>& track, std::int32_t time_q12) const {
    const std::int32_t frame = frame_of(time_q12);
    const std::int32_t fraction = time_q12 & (kTimeOne - 1);
    std::int32_t from = frame;
    std::int32_t to = frame;
    std::int32_t alpha = 0;
    switch (phase_at(segments, frame)) {
        case Phase::Blend:
            to = frame + 1;
            alpha = fraction;
            break;
        case Phase::Still:
            break;
        case Phase::ShotEnd:
            // Keep the motion of the shot's last two frames rather than blending into the next shot.
            from = frame - 1;
            alpha = fraction + kTimeOne;
            break;
    }
    return {track.key_for_frame(from), track.key_for_frame(to), alpha};
}

template Blend Cutscene::blend_at(const Track<ActorKey>&, std::int32_t) const;
template Blend Cutscene::blend_at(const Track<CameraKey>&, std::int32_t) const;

ActorPose Cutscene::actor_pose(std::size_t actor, std::int32_t time_q12) const {
    ActorPose pose;
    if (actor >= actors.size() || actors[actor].keys.empty()) {
        return pose;
    }
    const ActorTrack& track = actors[actor];
    const Blend b = blend_at(track, time_q12);
    const ActorKey& k0 = track.keys[b.from];
    const ActorKey& k1 = track.keys[b.to];
    for (std::size_t i = 0; i < 3; ++i) {
        pose.position[i] = to_world(blend_s16(k0.position[i], k1.position[i], b.alpha), origin[i]);
        pose.rotation[i] = blend_angle(k0.rotation[i], k1.rotation[i], b.alpha);
    }
    return pose;
}

CameraPose Cutscene::camera_pose(std::int32_t time_q12) const {
    CameraPose pose;
    if (camera.keys.empty()) {
        return pose;
    }
    const Blend b = blend_at(camera, time_q12);
    const CameraKey& k0 = camera.keys[b.from];
    const CameraKey& k1 = camera.keys[b.to];
    for (std::size_t i = 0; i < 3; ++i) {
        pose.eye[i] = to_world(blend_s16(k0.eye[i], k1.eye[i], b.alpha), origin[i]);
        pose.target[i] = to_world(blend_s16(k0.target[i], k1.target[i], b.alpha), origin[i]);
    }
    pose.roll = blend_angle(k0.roll, k1.roll, b.alpha);
    pose.projection = blend_s16(k0.projection, k1.projection, b.alpha);
    return pose;
}

}  // namespace sh::cutscene
