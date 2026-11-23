// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/effects/sfx_mix.h"

#include <algorithm>

namespace sh::effects {

SfxMixOut SfxMix::update(std::int32_t balance, std::int32_t base_volume, std::int32_t distance, std::int32_t range,
                         SfxMixMode mode, std::int32_t delta_time) {
    std::int32_t pan = balance;
    if (mode == SfxMixMode::Centered) {
        pan = 0;
    } else if (mode == SfxMixMode::Smoothed) {
        pan = balance >> 1;
        const std::int32_t step = static_cast<std::int32_t>((static_cast<std::int64_t>(delta_time) * p_.slew_rate) >> 12);
        std::int32_t cur = pan_;
        if (cur < pan) {
            cur = static_cast<std::int8_t>(std::min(pan, cur + step));
        }
        if (pan < cur) {
            cur = static_cast<std::int8_t>(std::max(pan, cur - step));
        }
        pan = cur;
    }
    pan_ = static_cast<std::int8_t>(pan);

    std::int32_t base = base_volume == 0 ? p_.default_volume : base_volume;
    if (mode != SfxMixMode::Direct) {
        base >>= 1;
    }

    // Distance fall-off in 32-bit arithmetic, wrapping like the original.
    const std::int32_t scaled = static_cast<std::int32_t>(static_cast<std::uint32_t>(base) * static_cast<std::uint32_t>(distance));
    const std::int32_t a = range != 0 ? scaled / range : 0;
    const std::int32_t t = a - (base - p_.volume_max);
    std::int32_t vol;
    if (t < 0) {
        vol = 0;
    } else if (t < p_.volume_max) {
        vol = a - (base + 1);
    } else {
        vol = p_.volume_max;
    }
    return SfxMixOut{pan, vol};
}

}  // namespace sh::effects
