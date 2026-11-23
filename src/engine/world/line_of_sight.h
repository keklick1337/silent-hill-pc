#pragma once

// Line-of-sight policies over an injected ray probe (src/engine/world/ray_query.h for the
// engine's own probe; the game passes its bridge). The integer pipeline is fixed to the last
// bit: Q.12 angles where 4096 is a full turn, 16-bit wraps where the game's angle fields wrap,
// and exactly one 15-bit rng draw per candidate, before the probe — the draw order is what the
// replay hash sees.
//
// Concepts:
//   Rays: callable (const Vec3& from, const Vec3& offset, bool with_characters) -> SightOutcome
//   Rng:  callable () -> std::int32_t, a 15-bit unsigned draw (the one primitive the game's
//         generator exposes; range draws are derived from it)

#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"
#include "engine/world/ray_query.h"

#include <cstdint>

namespace sh::world {

// What one ray told the policy.
struct SightOutcome {
    bool blocked = false;        // the ray stopped: geometry or a character volume
    bool hit_character = false;  // the stop was a character volume
};

// The watcher. `heading` is a Q.12 angle; `box_offset_y` is the half-height of its eye band.
struct LosHead {
    Vec3 position;
    std::int32_t heading = 0;
    std::int32_t box_offset_y = 0;
};

// The watched body.
struct LosBody {
    Vec3 position;
    std::int32_t box_top = 0;       // head-band bound above its origin
    std::int32_t box_offset_y = 0;  // band half-height
    bool head_band = false;         // low stance: the ray runs flat at head height
};

// Normalises a Q.12 angle into (-2048, 2048]; a half turn stays +2048.
std::int32_t normalize_heading(std::int32_t angle);

// Horizontal distance |(dx, dz)| in Q.12, squared in Q.6 so big separations cannot overflow.
std::int32_t planar_distance_q12(std::int32_t dx, std::int32_t dz);

// The walk probe (dist, heading): (dist * sin, 0, dist * cos) in Q.12, wrap-multiplied.
Vec3 heading_offset(std::int32_t dist, std::int32_t heading);

// Probes the candidate headings around `heading` and returns the smallest clear turn as a
// normalised Q.12 angle, or 4096 when every candidate is blocked. Fifteen candidates: one
// random draw near the heading first, then widening even/odd offsets; each offset draws once
// before its probe, and -1 is the "nothing found" mark (a candidate of exactly -1 counts as
// nothing, exactly as the game's sentinel does).
template <class Rays, class Rng>
std::int32_t clear_heading(const LosHead& head, std::int32_t dist, std::int32_t heading, Rays&& rays, Rng&& rng) {
    constexpr std::int32_t kNone = -1;
    constexpr auto wrap16 = [](std::int32_t v) { return static_cast<std::int32_t>(static_cast<std::int16_t>(v)); };
    std::int32_t best = kNone;
    for (std::int32_t i = 0; i < 15; ++i) {
        const std::uint32_t draw = static_cast<std::uint32_t>(rng());
        std::int32_t candidate;
        if (i == 0) {
            candidate = wrap16(static_cast<std::int32_t>(draw % 64) - 32);
        } else if (i & 1) {
            candidate = wrap16((256 << ((i + 1) >> 1)) + static_cast<std::int32_t>(draw % 64));
        } else {
            candidate = wrap16(-(256 << (i >> 1)) - static_cast<std::int32_t>(draw % 64));
        }
        if (best != kNone) {
            const auto abs_of = [](std::int32_t v) { return v < 0 ? -v : v; };
            if (abs_of(best) < abs_of(candidate)) {
                continue;
            }
        }
        const std::int32_t probe = wrap16(candidate + heading);
        if (!rays(head.position, heading_offset(dist, probe), false).blocked) {
            best = candidate;
        }
    }
    if (best != kNone) {
        return normalize_heading(wrap16(best + heading));
    }
    return 4096;
}

// Sweeps `span` around the heading (a full turn sweeps twelve headings of its own) and returns
// the clear heading whose landing point lies farthest from (`clockwise`: nearest to)
// (to_x, to_z), as a normalised Q.12 angle, or 4096 when nothing is clear. Each candidate
// draws once before its probe; the landing distances are squared in Q.6.
template <class Rays, class Rng>
std::int32_t heading_toward(const LosHead& head, std::int32_t dist, std::int32_t to_x, std::int32_t to_z,
                            std::int32_t span, bool clockwise, Rays&& rays, Rng&& rng) {
    constexpr std::int32_t kSpanSteps = 3;
    constexpr std::int32_t kFullTurn = 4096;
    constexpr std::int32_t kNone = -4096;
    constexpr auto wrap16 = [](std::int32_t v) { return static_cast<std::int32_t>(static_cast<std::int16_t>(v)); };

    std::int32_t step = static_cast<std::int16_t>(span / kSpanSteps);
    if (step == 0) {
        // The modulo below would divide by zero; a tiny span means one straight probe.
        step = 1;
    }
    const std::int32_t count = span == kFullTurn ? 12 : 7;
    std::int32_t best_dist = clockwise ? 0x7FFFFFFF : 0;
    std::int32_t best_angle = kNone;

    for (std::int32_t i = 0; i < count; ++i) {
        // Signed remainders: the span step may be negative.
        const std::int32_t draw = rng();
        std::int32_t angle;
        if (span == kFullTurn) {
            angle = wrap16(((i * 30) + (draw % 30)) * 4096 / 360);
        } else {
            angle = wrap16(head.heading + ((i - kSpanSteps) * step) + ((draw % step) >> 1) - (step >> 2));
        }
        const Vec3 offset = heading_offset(dist, angle);
        if (!rays(head.position, offset, false).blocked) {
            const std::int32_t land_x = head.position.x + offset.x;
            const std::int32_t land_z = head.position.z + offset.z;
            const std::int32_t dx = (to_x - land_x) >> 6;
            const std::int32_t dz = (to_z - land_z) >> 6;
            const std::int32_t land_dist =
                wrap_add(wrap_mul(dx, dx), wrap_mul(dz, dz));
            if ((!clockwise && best_dist < land_dist) || (clockwise && land_dist < best_dist)) {
                best_dist = land_dist;
                best_angle = angle;
            }
        }
    }
    if (best_angle != kNone) {
        return normalize_heading(best_angle);
    }
    return kFullTurn;
}

// Whether the watcher sees the body: the reach shrinks with how far the body is off the
// watcher's heading (doubled each of `count` steps), then one solid ray with characters tells
// the answer — blocked by geometry alone means unseen, stopped by (or passing through) a
// character means seen.
template <class Rays>
bool is_visible(const LosHead& watcher, const LosBody& body, std::int32_t count, std::int32_t base_dist_max,
                std::int32_t dist_step, Rays&& rays) {
    const std::int32_t dx = body.position.x - watcher.position.x;
    const std::int32_t dz = body.position.z - watcher.position.z;
    const std::int32_t rel = normalize_heading(fixed_atan2(dx, dz) - watcher.heading);
    std::int32_t dist_mult = rel < 0 ? (rel * 2) + 4096 : (2048 - rel) * 2;
    const std::int32_t factor = dist_mult;  // every step multiplies by the initial value
    for (std::int32_t c = 0; c < count; ++c) {
        dist_mult = static_cast<std::int32_t>((static_cast<std::int64_t>(dist_mult) * factor) >> 12);
    }

    const std::int32_t dist = planar_distance_q12(dx, dz);
    const std::int32_t dist_max =
        base_dist_max + static_cast<std::int32_t>((static_cast<std::int64_t>(dist_step) * dist_mult) >> 12);
    if (dist_max < dist) {
        return false;
    }

    Vec3 pos = watcher.position;
    Vec3 offset{dx, 0, dz};
    if (body.head_band) {
        pos.y = body.position.y + body.box_top;
    } else {
        pos.y = watcher.position.y + watcher.box_offset_y;
        offset.y = (body.position.y + body.box_offset_y) - (watcher.position.y - watcher.box_offset_y);
    }
    const SightOutcome sight = rays(pos, offset, true);
    return !sight.blocked || sight.hit_character;
}

// The sight probe (rays(..., false)) from `from` along `offset` stopped on geometry: a character in the way counts as
// reaching it.
template <class Rays>
bool wall_in_way(const Vec3& from, const Vec3& offset, Rays&& rays) {
    const SightOutcome sight = rays(from, offset, false);
    return sight.blocked && !sight.hit_character;
}

// Geometry between the watcher at `from` and the point `to`.
template <class Rays>
bool wall_toward_point(const Vec3& from, const Vec3& to, Rays&& rays) {
    return wall_in_way(from, Vec3{wrap_add(to.x, -from.x), wrap_add(to.y, -from.y), wrap_add(to.z, -from.z)}, rays);
}

// Geometry between a character at `from` and the body at (to_x, to_z): the probe runs from the character's origin and
// dips 0.1 m over the whole distance.
template <class Rays>
bool wall_toward_body(const Vec3& from, std::int32_t to_x, std::int32_t to_z, Rays&& rays) {
    constexpr std::int32_t kDip = -409;  // -0.1 in Q.12, truncated as the game's constant is
    return wall_in_way(from, Vec3{wrap_add(to_x, -from.x), kDip, wrap_add(to_z, -from.z)}, rays);
}

// Geometry `dist` ahead along `heading` at the character's own height.
template <class Rays>
bool wall_ahead(const Vec3& from, std::int32_t dist, std::int32_t heading, Rays&& rays) {
    const Vec3 offset = heading_offset(dist, heading);
    return wall_toward_point(from, Vec3{wrap_add(from.x, offset.x), from.y, wrap_add(from.z, offset.z)}, rays);
}

// Anything `dist` ahead along `heading` at the character's own height: geometry or a character.
template <class Rays>
bool blocked_ahead(const Vec3& from, std::int32_t dist, std::int32_t heading, Rays&& rays) {
    return rays(from, heading_offset(dist, heading), false).blocked;
}

}  // namespace sh::world
