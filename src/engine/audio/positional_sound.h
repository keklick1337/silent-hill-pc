#pragma once

// Sound effects placed in the world: how loud a source is at the
// listener (distance from the player to the source, and from the camera to the player), its stereo
// balance, and which request of the sound driver plays it.
//
// The distances and the balance come from the caller (they depend on the camera and the player);
// these functions make the decisions. Volumes are 0 (silent) .. 255 (full) on the way in; the driver
// requests carry the driver's attenuation (255 - volume).
//
// Bit-exactness contract: the results equal the original game's to the last bit.

#include <cstdint>

namespace sh::audio {

// 2^(-x) for a Q.12 x, from a 33-entry table of one octave with linear interpolation (Q.12 result).
std::int32_t pow2_neg(std::int32_t x);
// ... limited to [0, 4.0].
std::int32_t pow2_neg_clamped(std::int32_t x);

// The volume left at the listener: the source fades past 6 m from the player, the whole scene past
// 2.5 m between camera and player. Lengths are Q.12.
std::int32_t distance_volume(std::int32_t camera_to_player, std::int32_t player_to_source, std::int32_t volume);

// Where a source is as the camera sees it: `view` is the camera's rotation matrix (Q.12 columns), `dir` the unit
// direction from the camera to the source (Q.12). The balance is the lateral component clamped to +-127; the azimuth
// is the angle from straight ahead (positive right, +-2048 behind) before the front/back information is dropped.
struct StereoPlacement {
    std::int32_t balance = 0;
    std::int32_t azimuth = 0;
};
StereoPlacement stereo_placement(const std::int16_t view[3][3], const std::int32_t dir[3]);

// Distances between two world points (Q.12) as the game measures them for sound:
// - precise_length: each axis squared in 64 bits and brought back to Q.12, the root in double precision (the
//   listener and source distances of placed sounds);
// - wrapped_length: plain 32-bit squares that wrap, the game's table root (a wrapped sum reads as 0; the engine's
//   sustained falloff loops);
// - coarse_length: the axes in Q.6 steps, the game's table root, back to Q.12 (the falloff fade of the original).
std::int32_t precise_length(std::int32_t dx, std::int32_t dy, std::int32_t dz);
std::int32_t wrapped_length(std::int32_t dx, std::int32_t dy, std::int32_t dz);
std::int32_t coarse_length(std::int32_t dx, std::int32_t dy, std::int32_t dz);

enum class SoundRequest : std::uint8_t { None, Play, Update, Step };

struct PlacedSound {
    SoundRequest request = SoundRequest::None;
    std::int32_t balance = 0;
    std::uint8_t attenuation = 0;  // the driver's volume argument: 0 loud .. 255 silent
};

// Flags of a placed effect.
enum PlaceFlags : std::uint32_t {
    kPlaceMono = 1u << 0,          // centred whatever the output
    kPlaceNoDistance = 1u << 1,    // no fading with distance
    kPlaceUpdate = 1u << 2,        // updates a sound that plays instead of starting one
};

struct Placement {
    bool mono_output = false;      // the options' mono output centres every sound
    std::int32_t balance = 0;      // stereo balance of the source as the camera sees it
    std::int32_t camera_to_player = 0;
    std::int32_t player_to_source = 0;
};

// A one-shot or updated effect (flags above).
PlacedSound place_effect(const Placement& at, std::int32_t volume, std::uint32_t flags);
// A footstep-like effect: always faded with distance, keyed with a pitch.
PlacedSound place_step(const Placement& at, std::int32_t volume);
// An updated loop that fades linearly to silence at `falloff` (Q.12) from the player. A negative
// volume plays nothing; a zero falloff counts as already past it.
PlacedSound place_falloff(const Placement& at, std::int32_t volume, std::int32_t falloff);

}  // namespace sh::audio
