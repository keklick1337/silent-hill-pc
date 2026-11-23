#include "engine/world/wall_detect.h"

namespace sh::world {

// The band arithmetic of the sixteen ring samples: the mid of the highest and lowest floor,
// clamped under the start height, and a 1/16 slide step along the bisector of the highest
// sample and the end of the above-mid spread. The counter-clockwise spread is dead in the game
// (its bound sits below its start index), so the bisector is the highest index plus the
// clockwise spread end.
RadialBand radial_band_from_heights(const std::int32_t* heights, std::int32_t start_height,
                                    std::int32_t keep_below) {
    std::int32_t high = -122880;  // Q.12 -30: below any floor
    std::int32_t low = 122880;    // Q.12 30: above any floor
    std::int32_t high_index = 0;
    for (std::int32_t i = 0; i < 16; ++i) {
        if (high < heights[i]) {
            high = heights[i];
            high_index = i;
        }
        if (heights[i] < low) {
            low = heights[i];
        }
    }

    std::int32_t mid = (high + low) >> 1;
    if (mid < start_height - keep_below) {
        mid = start_height - keep_below;
    }

    std::int32_t spread_end = high_index;
    for (std::int32_t i = high_index + 1; i < high_index + 16 && mid < heights[i & 0xF]; ++i) {
        spread_end = i;
    }

    const std::int32_t angle = ((high_index + spread_end) << 8) >> 1;
    return RadialBand{mid, static_cast<std::int32_t>((static_cast<std::int64_t>(fixed_sin(angle)) * 256) >> 12),
                      static_cast<std::int32_t>((static_cast<std::int64_t>(fixed_cos(angle)) * 256) >> 12)};
}

}  // namespace sh::world
