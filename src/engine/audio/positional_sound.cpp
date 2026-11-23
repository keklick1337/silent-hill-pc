#include "engine/audio/positional_sound.h"

#include <array>
#include <cmath>
#include <limits>

#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"

namespace sh::audio {

namespace {

constexpr std::int32_t kOne = 4096;
constexpr std::int32_t kFull = 255;

// 2^(-i/32) in Q.12 for i = 0..32, as the game's table has them (entry 1 is off the curve: 0.9876
// instead of 0.9786; kept).
constexpr std::array<std::int32_t, 33> kOctave = {
    4096, 4045, 3922, 3838, 3756, 3675, 3596, 3519, 3443, 3370, 3298, 3227, 3158, 3090, 3024, 2959, 2896,
    2834, 2773, 2714, 2655, 2598, 2543, 2488, 2435, 2383, 2332, 2282, 2233, 2185, 2138, 2092, 2048};

std::int32_t mul_q12(std::int64_t a, std::int64_t b) { return static_cast<std::int32_t>((a * b) >> 12); }

// Shifts by a whole number of octaves; large left shifts wrap as 32-bit values.
std::int32_t octave_shift(std::int32_t v, std::int32_t octaves) {
    if (octaves > 0) {
        return v >> octaves;
    }
    if (octaves < 0) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << -octaves);
    }
    return v;
}

std::int32_t clamp_volume(std::int32_t v) { return v > kFull ? kFull : (v < 0 ? 0 : v); }

std::uint8_t attenuation_of(std::int32_t volume) { return static_cast<std::uint8_t>(~volume); }

}  // namespace

std::int32_t pow2_neg(std::int32_t x) {
    const std::int32_t octaves = x >> 12;
    if (octaves >= 12) {
        return 0;
    }
    if (octaves < -20) {
        return std::numeric_limits<std::int32_t>::max();
    }
    const std::int32_t step = (x & 0xFFF) >> 7;
    const std::int32_t blend = (x & 0x7F) << 5;
    std::int32_t value = octave_shift(kOctave[step], octaves);
    if (blend != 0) {
        const std::int32_t next = octave_shift(kOctave[step + 1], octaves);
        value = mul_q12(value, kOne - blend) + mul_q12(next, blend);
    }
    return value;
}

std::int32_t pow2_neg_clamped(std::int32_t x) {
    const std::int32_t v = pow2_neg(x);
    return v > 4 * kOne ? 4 * kOne : (v < 0 ? 0 : v);
}

std::int32_t distance_volume(std::int32_t camera_to_player, std::int32_t player_to_source, std::int32_t volume) {
    std::int32_t scene = pow2_neg_clamped((camera_to_player - 5 * kOne / 2) / 10);
    if (scene > kOne) {
        scene = kOne;
    }
    const std::int32_t source = pow2_neg_clamped((player_to_source - 6 * kOne) / 4);
    std::int32_t gain = mul_q12(scene, source);
    // Q.8 limits: at most 32.
    gain = gain > 32 * 256 ? 32 * 256 : (gain < 0 ? 0 : gain);
    return clamp_volume(mul_q12(volume, gain));
}

PlacedSound place_effect(const Placement& at, std::int32_t volume, std::uint32_t flags) {
    PlacedSound s;
    s.balance = (flags & kPlaceMono) || at.mono_output ? 0 : at.balance;
    volume = clamp_volume(volume);
    std::int32_t heard = flags & kPlaceNoDistance ? volume : distance_volume(at.camera_to_player, at.player_to_source, volume);
    if (heard > kFull) {
        heard = kFull;
    }
    s.request = flags & kPlaceUpdate ? SoundRequest::Update : SoundRequest::Play;
    s.attenuation = attenuation_of(heard);
    return s;
}

PlacedSound place_step(const Placement& at, std::int32_t volume) {
    PlacedSound s;
    s.balance = at.mono_output ? 0 : at.balance;
    std::int32_t heard = distance_volume(at.camera_to_player, at.player_to_source, clamp_volume(volume));
    if (heard > kFull) {
        heard = kFull;
    }
    s.request = SoundRequest::Step;
    s.attenuation = attenuation_of(heard);
    return s;
}

PlacedSound place_falloff(const Placement& at, std::int32_t volume, std::int32_t falloff) {
    PlacedSound s;
    s.balance = at.mono_output ? 0 : at.balance;
    if (volume > kFull) {
        volume = kFull;
    }
    if (volume < 0) {
        return s;
    }
    // Linear in distance: volume x distance / falloff. A zero falloff gives -1 (the console's
    // division by zero, which the rain loop relies on).
    const std::int32_t faded = falloff == 0 ? -1
                                            : static_cast<std::int32_t>(static_cast<std::uint32_t>(volume) *
                                                                        static_cast<std::uint32_t>(at.player_to_source)) /
                                                  falloff;
    const std::int32_t past = faded - (volume - kFull);
    std::uint16_t attenuation = 0;
    if (past >= 0) {
        attenuation = kFull;
        if (past < kFull) {
            attenuation = static_cast<std::uint8_t>(faded - (volume + 1));
        }
    }
    s.request = SoundRequest::Update;
    s.attenuation = static_cast<std::uint8_t>(attenuation);
    return s;
}

StereoPlacement stereo_placement(const std::int16_t view[3][3], const std::int32_t dir[3]) {
    // 32-bit products wrap like the game's arithmetic.
    auto dot = [&](int column) {
        std::uint32_t sum = 0;
        for (int row = 0; row < 3; ++row) {
            sum += static_cast<std::uint32_t>(static_cast<std::int32_t>(view[row][column])) *
                   static_cast<std::uint32_t>(dir[row]);
        }
        return static_cast<std::int32_t>(sum) >> 17;
    };
    const std::int32_t lateral = dot(0);
    StereoPlacement out;
    out.balance = lateral < -127 ? -127 : (lateral > 127 ? 127 : lateral);
    out.azimuth = world::fixed_atan2(lateral, dot(2));
    return out;
}

std::int32_t precise_length(std::int32_t dx, std::int32_t dy, std::int32_t dz) {
    auto square = [](std::int32_t v) {
        return static_cast<std::int32_t>((static_cast<std::int64_t>(v) * v) >> 12);
    };
    const std::int32_t sum = world::wrap_add(world::wrap_add(square(dx), square(dy)), square(dz));
    if (sum <= 0) {
        return 0;
    }
    return static_cast<std::int32_t>(std::sqrt(static_cast<double>(sum)) * 64.0);
}

std::int32_t wrapped_length(std::int32_t dx, std::int32_t dy, std::int32_t dz) {
    const std::int32_t sum =
        world::wrap_add(world::wrap_add(world::wrap_mul(dx, dx), world::wrap_mul(dy, dy)), world::wrap_mul(dz, dz));
    return world::game_sqrt(sum);
}

std::int32_t coarse_length(std::int32_t dx, std::int32_t dy, std::int32_t dz) {
    return wrapped_length(dx >> 6, dy >> 6, dz >> 6) << 6;
}

}  // namespace sh::audio
