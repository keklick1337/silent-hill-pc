// Placed sound effects (engine/audio/positional_sound.h).

#include "engine/audio/positional_sound.h"

#include "framework/sh_test.h"

namespace {

namespace au = sh::audio;

constexpr std::int32_t kOne = 4096;

}  // namespace

SH_TEST(positional_pow2_neg) {
    SH_CHECK(au::pow2_neg(0) == kOne);
    SH_CHECK(au::pow2_neg(kOne) == kOne / 2);
    SH_CHECK(au::pow2_neg(-kOne) == 2 * kOne);
    SH_CHECK(au::pow2_neg(12 * kOne) == 0);
    // Half an octave: table entry 16 (0.7071).
    SH_CHECK(au::pow2_neg(kOne / 2) == 2896);
    // Between entries: interpolated.
    const std::int32_t mid = au::pow2_neg(64);
    SH_CHECK(mid < 4096 && mid > 4045);
    SH_CHECK(au::pow2_neg_clamped(-3 * kOne) == 4 * kOne);
}

SH_TEST(positional_distance_volume) {
    // Close to the camera and the player the volume is doubled (then limited to 255).
    SH_CHECK(au::distance_volume(0, 0, 100) == 200);
    // 10 m past 6 m from the player: 2^(-10/4) of the volume.
    const std::int32_t far = au::distance_volume(0, 16 * kOne, 255);
    SH_CHECK(far > 40 && far < 50);
    SH_CHECK(au::distance_volume(0, 0, 400) == 255);
}

SH_TEST(positional_requests) {
    au::Placement at;
    at.balance = -40;
    const au::PlacedSound play = au::place_effect(at, 300, 0);
    SH_CHECK(play.request == au::SoundRequest::Play && play.attenuation == 0 && play.balance == -40);
    const au::PlacedSound mono = au::place_effect(at, 100, au::kPlaceMono | au::kPlaceUpdate | au::kPlaceNoDistance);
    SH_CHECK(mono.request == au::SoundRequest::Update && mono.balance == 0 && mono.attenuation == 155);
    at.mono_output = true;
    SH_CHECK(au::place_step(at, 255).balance == 0);
    // Falloff: half way to the falloff distance.
    at.player_to_source = 5 * kOne;
    const au::PlacedSound half = au::place_falloff(at, 200, 10 * kOne);
    SH_CHECK(half.request == au::SoundRequest::Update && half.attenuation == static_cast<std::uint8_t>(100 - 201));
    SH_CHECK(au::place_falloff(at, -1, kOne).request == au::SoundRequest::None);
    // A zero falloff plays a full-volume loop at full volume (the rain).
    SH_CHECK(au::place_falloff(at, 255, 0).attenuation == 0);
}
