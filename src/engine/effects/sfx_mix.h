// SPDX-License-Identifier: GPL-3.0-or-later
// Pan and volume of a looping positional sound (the carousel music of map6_s04): the pan follows
// the stereo balance of the source, optionally halved and slewed toward the target at a fixed
// rate; the volume falls with the distance to the source. The caller measures the balance and the
// distance and applies the result to the sound channel.

#pragma once

#include <cstdint>

namespace sh::effects {

struct SfxMixParams {
    std::int32_t slew_rate = 0xC0;       // Q12 per second, pan slew in the smoothed mode
    std::int32_t default_volume = 0x80;  // used when the caller passes no base volume
    std::int32_t volume_max = 0xFF;
};

enum class SfxMixMode : std::int32_t { Direct = 0, Smoothed = 1, Centered = 2 };

struct SfxMixOut {
    std::int32_t pan = 0;
    std::int32_t volume = 0;
};

class SfxMix {
public:
    explicit SfxMix(SfxMixParams p = {}) : p_(p) {}

    // balance: stereo balance of the source, distance: Q12 distance to it, range: Q12 distance at
    // which the volume reaches the fade point, delta_time: Q12 seconds of the frame.
    SfxMixOut update(std::int32_t balance, std::int32_t base_volume, std::int32_t distance, std::int32_t range,
                     SfxMixMode mode, std::int32_t delta_time);

    void reset() { pan_ = 0; }
    std::int32_t pan() const { return pan_; }

private:
    SfxMixParams p_;
    std::int8_t pan_ = 0;  // one byte, like the stored original; the cast wraps
};

}  // namespace sh::effects
