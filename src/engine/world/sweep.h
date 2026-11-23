#pragma once

// Shared pieces of the swept queries (rays and character movement): a circle moving along a segment against a
// point, and the vector unit's fixed-point scale. Integer results equal the original game's.

#include "engine/world/fixed_math.h"

#include <cstdint>

namespace sh::world {

// The value the sweep gives when the circle never reaches the point.
inline constexpr std::int16_t kNoSweepHit = -1;

// A circle of `radius` (Q.8) moves from the origin along the unit direction (dir_x, dir_z) for `length` (Q.8). The
// result is the fraction of the way (Q.12, 0..1) at which it first touches the point (dx, dz), 0 when it touches at
// the start, kNoSweepHit when it never does. All inputs are 16-bit like the vector unit's registers.
constexpr std::int16_t sweep_circle_to_point(std::int16_t dir_x, std::int16_t dir_z, std::int16_t length,
                                             std::int16_t dx, std::int16_t dz, std::int16_t radius) {
    const EdgeFrame f = to_edge_frame(dir_x, dir_z, dx, dz);
    std::int16_t distance;
    if (f.along < 0) {
        distance = static_cast<std::int16_t>(planar_length(f.along, f.across));
    } else if (length < f.along) {
        distance = static_cast<std::int16_t>(planar_length(static_cast<std::int16_t>(f.along - length), f.across));
    } else {
        distance = static_cast<std::int16_t>(f.across < 0 ? -f.across : f.across);
    }
    if (distance >= radius) {
        return kNoSweepHit;
    }
    if (length == 0) {
        return 0;
    }
    const std::int32_t enter = f.along - game_sqrt(radius * radius - f.across * f.across);
    auto fraction = static_cast<std::int16_t>((enter << 12) / length);
    if (fraction < 0) {
        fraction = 0;
    } else if (fraction > 4096) {
        fraction = 4096;
    }
    return fraction;
}

// The vector unit's "scale by IR0": (factor * component) >> 12 with both operands taken as 16-bit and the result
// saturated to 16 bits.
constexpr std::int16_t scale_q12_16(std::int32_t factor, std::int32_t component) {
    const std::int32_t product =
        (static_cast<std::int16_t>(factor) * static_cast<std::int16_t>(component)) >> 12;
    if (product > 0x7FFF) {
        return 0x7FFF;
    }
    if (product < -0x8000) {
        return -0x8000;
    }
    return static_cast<std::int16_t>(product);
}

}  // namespace sh::world
