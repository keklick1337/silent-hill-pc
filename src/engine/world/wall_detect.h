#pragma once

// Wall, ledge and void classification of the mover's ground (the movement pass asks what the
// surface under a character means): a ring of surface samples decides whether the character
// faces a wall, a forgiving ledge or a hole. The integer pipeline — probe count, angles,
// comparisons and the mid-band arithmetic — is fixed to the game's values; the surface itself
// comes from an injected query.
//
//   Surfaces: callable (std::int32_t x, std::int32_t z) -> WallProbeSample, Q.12 world coordinates

#include "engine/world/line_of_sight.h"

#include <cstdint>

namespace sh::world {

// One surface sample: the ground height at the point and its material. The material is opaque;
// the scan only ever compares it to the "no ground at all" type the caller names.
struct WallProbeSample {
    std::int32_t height = 0;  // Q.12
    std::uint8_t ground_type = 0;
};

// What the mover stands on right now.
struct WallSurface {
    std::int32_t height = 0;  // Q.12
    std::uint8_t ground_type = 0;
};

enum class WallKind : std::uint8_t {
    Clear,  // on solid ground, no significant drop ahead
    Wall,   // the ground ahead drops below the wall bound
    Void,   // no ground at all under the mover: look for a floor around it
};

struct WallVerdict {
    WallKind kind = WallKind::Clear;
    std::int32_t wall_count = 0;        // probes that fell below the wall bound (Wall)
    std::int32_t void_height = 0;       // the last probe that found a floor (Void)
    std::uint8_t void_ground_type = 0;  // stays at the void type when nothing was found
    bool forgive = false;  // a wall held by fewer probes than the threshold is a ledge
};

struct WallScan {
    std::int32_t x = 0;  // the mover's position, Q.12
    std::int32_t z = 0;
    std::int32_t height = 0;           // the mover's height, Q.12
    WallSurface standing;              // the surface directly under the mover
    std::uint8_t void_ground_type = 0;  // the ground type that means "no floor"
    std::int32_t drop = 2048;          // Q.12 0.5: a lower surface ahead is a wall
    std::int32_t probe_radius = 819;   // Q.12 0.2: the sample ring around the mover
    std::int32_t probe_step = 467;     // 370/9 of a turn per sample — 370, not 360: the game's own step
    std::int32_t forgive_below = 3;    // a wall with fewer samples below the bound is forgiven
};

// Classifies. Nine samples on the ring; a Wall counts its samples below the drop bound, a Void
// keeps the last sample that found any floor. Clear answers without a single query.
template <class Surfaces>
WallVerdict classify_wall(const WallScan& scan, Surfaces&& surfaces) {
    WallVerdict verdict;
    verdict.void_ground_type = scan.void_ground_type;

    const std::int32_t bound = scan.height - scan.drop;
    if (scan.standing.ground_type == scan.void_ground_type) {
        verdict.kind = WallKind::Void;
    } else {
        verdict.kind = scan.standing.height < bound ? WallKind::Wall : WallKind::Clear;
    }
    if (verdict.kind == WallKind::Clear) {
        return verdict;
    }

    for (std::int32_t i = 0; i < 9; ++i) {
        const Vec3 ring = heading_offset(scan.probe_radius, i * scan.probe_step);
        const WallProbeSample sample = surfaces(scan.x + ring.x, scan.z + ring.z);
        if (verdict.kind == WallKind::Wall) {
            if (sample.height < bound) {
                ++verdict.wall_count;
            }
        } else if (sample.ground_type != scan.void_ground_type) {
            verdict.void_height = sample.height;
            verdict.void_ground_type = sample.ground_type;
        }
    }
    verdict.forgive = verdict.kind == WallKind::Wall && verdict.wall_count < scan.forgive_below;
    return verdict;
}

// The ground band around a point, for the mover that lost its ground: sixteen samples on a unit
// circle find the highest and lowest floor, and the slide offset points away from the drop.
struct RadialBand {
    std::int32_t mid_height = 0;  // the middle of the highest and lowest floor, Q.12
    std::int32_t offset_x = 0;    // Q.12, a step of 1/16 away from the drop
    std::int32_t offset_z = 0;
};

struct RadialScan {
    std::int32_t x = 0;  // Q.12
    std::int32_t z = 0;
    std::int32_t start_height = 0;  // the height the mover fell from, Q.12
    std::int32_t heading = 0;       // Q.12 angle; only its low 4 bits nudge the first sample
    std::int32_t keep_below = 409;  // Q.12 0.1: the mid never falls further under the start
};

// Band arithmetic over the sixteen collected heights (wall_detect.cpp).
RadialBand radial_band_from_heights(const std::int32_t* heights, std::int32_t start_height,
                                    std::int32_t keep_below);

// Samples the ring (22.5 degrees apart, starting at the heading's low 4 bits) and hands the
// heights to the band arithmetic.
template <class Surfaces>
RadialBand radial_ground_band(const RadialScan& scan, Surfaces&& surfaces) {
    std::int32_t heights[16];
    const std::int32_t base = scan.heading & 0xF;
    for (std::int32_t i = 0; i < 16; ++i) {
        const std::int32_t angle = base + (i << 8);
        const WallProbeSample sample = surfaces(scan.x + fixed_sin(angle), scan.z + fixed_cos(angle));
        heights[i] = sample.height;
    }
    return radial_band_from_heights(heights, scan.start_height, scan.keep_below);
}

}  // namespace sh::world
