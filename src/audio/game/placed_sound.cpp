// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game's placed sound effects on the engine (engine/audio/positional_sound.h section
// 9b): the camera, the player and the source give the distances and the stereo balance (Session.audio), the engine
// decides the driver request. Also the engine's sustain of positional loops and the carousel music mix of map6_s04.
// The Sfx_* and Pc_* functions are the game's entry points.

#include <cstdint>

#include "audio/game/game_sound_driver.h"
#include "engine/audio/loop_sustain.h"
#include "engine/audio/positional_sound.h"
#include "engine/effects/sfx_mix.h"
#include "engine/game_state/session_audio.h"

namespace sh::audio {
// A world point as the game's code passes it (three Q12 words).
struct WorldPoint {
    std::int32_t x, y, z;
};
}  // namespace sh::audio

namespace {

namespace au = ::sh::audio;
namespace sa = ::sh::game_state::audio;
using au::WorldPoint;

// The game's placement flags.
constexpr std::int32_t kFlagMono = 1 << 0;
constexpr std::int32_t kFlagNoDistance = 1 << 1;
constexpr std::int32_t kFlagUpdate = 1 << 2;
constexpr std::int32_t kMaxVolume = 0xFF;

sa::Point point_of(const WorldPoint& v) { return {v.x, v.y, v.z}; }

std::int32_t sub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

std::int32_t precise_between(const sa::Point& a, const sa::Point& b) {
    return au::precise_length(sub(a.x, b.x), sub(a.y, b.y), sub(a.z, b.z));
}

au::Placement placement_of(const WorldPoint* pos, bool balance) {
    au::Placement at;
    at.mono_output = sa::mono_output();
    // Map code passes a null position with Mono | NoDistance; the original never read it then.
    at.balance = (pos != nullptr && balance && !at.mono_output) ? sa::stereo_balance(point_of(*pos)) : 0;
    const sa::Point player = sa::player_position();
    at.camera_to_player = precise_between(sa::listener_position(), player);
    at.player_to_source = pos != nullptr ? precise_between(player, point_of(*pos)) : 0;
    return at;
}

std::uint32_t place_flags(std::int32_t flags) {
    std::uint32_t f = 0;
    if (flags & kFlagMono) {
        f |= au::kPlaceMono;
    }
    if (flags & kFlagNoDistance) {
        f |= au::kPlaceNoDistance;
    }
    if (flags & kFlagUpdate) {
        f |= au::kPlaceUpdate;
    }
    return f;
}

void send(const au::PlacedSound& s, int id, int pitch) {
    au::SoundDriver& driver = au::game_sound_driver();
    const int balance = static_cast<std::int8_t>(s.balance);
    const auto sound = static_cast<std::uint16_t>(id);
    switch (s.request) {
    case au::SoundRequest::Play:
        driver.play_effect(sound, balance, s.attenuation);
        break;
    case au::SoundRequest::Update:
        driver.update_effect(sound, balance, s.attenuation, static_cast<std::int8_t>(pitch));
        break;
    case au::SoundRequest::Step:
        driver.play_step(sound, balance, s.attenuation, static_cast<std::int8_t>(pitch));
        break;
    case au::SoundRequest::None:
        break;
    }
}

// Placement of a falloff loop from its player distance.
au::Placement falloff_placement(const sa::Point& pos, std::int32_t player_to_source) {
    au::Placement at;
    at.mono_output = sa::mono_output();
    at.balance = at.mono_output ? 0 : sa::stereo_balance(pos);
    at.player_to_source = player_to_source;
    return at;
}

au::LoopSustain& loops() {
    static au::LoopSustain sustain;
    return sustain;
}

sh::effects::SfxMix& carousel_mix() {
    static sh::effects::SfxMix mix;
    return mix;
}

}  // namespace

using sh::audio::WorldPoint;

extern "C" {

void Sfx_WithFlagsAndPitchPlay(int sfxId, const WorldPoint* pos, std::int32_t vol, std::int32_t flags,
                               std::int32_t pitch) {
    send(au::place_effect(placement_of(pos, !(flags & kFlagMono)), vol, place_flags(flags)), sfxId, pitch);
}

void Sfx_WithFlagsPlay(int sfxId, const WorldPoint* pos, std::int32_t vol, std::int32_t flags) {
    Sfx_WithFlagsAndPitchPlay(sfxId, pos, vol, flags, 0);
}

void Sfx_WithPitchPlay(int sfxId, const WorldPoint* pos, std::int32_t vol, std::int8_t pitch) {
    send(au::place_step(placement_of(pos, true), vol), sfxId, pitch);
}

// The original's falloff fade: the distance in Q.6 steps.
void Sfx_WithFalloffAndPitchPlay(int sfxId, WorldPoint* pos, std::int32_t vol, std::int32_t falloff,
                                 std::int8_t pitch) {
    const sa::Point at = point_of(*pos);
    const sa::Point player = sa::player_position();
    const std::int32_t distance = au::coarse_length(sub(player.x, at.x), sub(player.y, at.y), sub(player.z, at.z));
    send(au::place_falloff(falloff_placement(at, distance), vol, falloff), sfxId, pitch);
}

// The engine's falloff loop: the distance as a plain 32-bit length, and the loop is sustained while it plays.
void Sfx_Play3dWithFalloff(int sfxId, WorldPoint* pos, std::int32_t vol, std::int32_t falloff, std::int8_t pitch) {
    const sa::Point at = point_of(*pos);
    if (vol > kMaxVolume) {
        vol = kMaxVolume;
    }
    // Effects on reserved voices (the radio loops) are restarted by their own update: never sustained.
    if (vol >= 0 && !au::game_sound_rules_resolved().fixed_voice(sfxId)) {
        loops().record({static_cast<std::uint16_t>(sfxId), {at.x, at.y, at.z}, vol, falloff, pitch});
    }
    const sa::Point player = sa::player_position();
    const std::int32_t distance = au::wrapped_length(sub(player.x, at.x), sub(player.y, at.y), sub(player.z, at.z));
    send(au::place_falloff(falloff_placement(at, distance), vol, falloff), sfxId, pitch);
}

// Once per frame, after the map and event code had their chance to update the loops.
void Pc_3dAudio_SustainPositionalLoops(void) {
    loops().sweep([](std::uint16_t id) { return au::game_sound_driver().effect_has_voice(id); },
                  [](const au::SustainedLoop& l) {
                      WorldPoint pos{l.position[0], l.position[1], l.position[2]};
                      Sfx_Play3dWithFalloff(l.id, &pos, l.volume, l.falloff, static_cast<std::int8_t>(l.pitch));
                  });
}

void Pc_3dAudio_SustainReset(void) { loops().reset(); }

// Carousel music mix of map6_s04. Returns the stored pan byte; outPan/outVolume are the values to apply.
void Pc_CarouselMix_Reset(void) { carousel_mix().reset(); }

int Pc_CarouselMix_Update(int balance, int baseVolume, int distance, int range, int mode, int deltaTime, int* outPan,
                          int* outVolume) {
    const sh::effects::SfxMixOut out = carousel_mix().update(balance, baseVolume, distance, range,
                                                             static_cast<sh::effects::SfxMixMode>(mode), deltaTime);
    *outPan = out.pan;
    *outVolume = out.volume;
    return carousel_mix().pan();
}

}  // extern "C"
