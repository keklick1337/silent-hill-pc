#pragma once

// Rays through the world: line of sight, bullets, weapon reach. A ray is traced
// against the wall edges and pillars of the placed cells and against character volumes; the nearest contact wins.
// Written from the behaviour of the original game; the results are the same
// integers.

#include "engine/world/ground_query.h"

#include <cstdint>
#include <span>

namespace sh::world {

struct Vec3 {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

enum class RayMode : std::uint8_t {
    // Bullets and probes: an edge stops the ray when a floor with a real material is on one of its sides; pillars
    // stop it when they have a material.
    Solid,
    // Sight and the movement probes: only one-sided edges (a floor on one side, a wall face on the other) stop the
    // ray, and only when it meets them from the open side; every pillar stops it.
    Sight,
};

// A placed cell as a ray sees it: geometry, where it is, and its visit ledger (shared with every other query).
using RayCell = ActiveCell;

// A character's volume for rays (Q.8 world units): a vertical cylinder around (x, z) between `low` and `high`.
// Y grows downwards, so `low` is the smaller value.
struct RayTarget {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int16_t low = 0;
    std::int16_t high = 0;
    std::int16_t radius = 0;
    int id = -1;  // handed back in RayHit::target
};

struct RayRequest {
    Vec3 from;    // Q.12 world
    Vec3 offset;  // Q.12, from the start to the end of the ray
    RayMode mode = RayMode::Solid;
    std::int16_t min_pillar_radius = 0;  // Q.8: thinner pillars are ignored
    std::uint16_t enabled_groups = 0;    // the active collision group mask
};

struct RayHit {
    bool hit = false;
    // The ray could not be set up (no horizontal extent). The game then reports the length its previous ray left
    // behind; the caller supplies that.
    bool rejected = false;
    std::int16_t length = 0;      // Q.8 horizontal length of the ray, when not rejected
    Vec3 point;                   // Q.12 world, the contact
    std::int32_t distance = 0;    // Q.12 horizontal distance to the contact
    std::int32_t ground_height = 0;  // Q.12: top of what was hit
    std::int16_t normal_x = 0;    // Q.8 direction away from what was hit (not normalised)
    std::int16_t normal_z = 0;
    std::uint8_t ground_type = 0;
    int target = -1;              // RayTarget::id of the character hit, -1 for geometry
};

// Traces the ray. `cells` are visited in order and their ledgers updated as the original pass updates them; then the
// targets. A miss leaves hit = false and only `length`/`rejected` set.
RayHit trace_ray(const RayRequest& request, std::span<const RayCell> cells, std::span<const RayTarget> targets);

// What the game records of a traced ray (Q.12 world). A miss reports the end of the ray, the ray's own length as the
// distance and the game's fixed miss height; a hit reports the contact and the facing angle of what was hit.
struct RayReport {
    bool hit = false;
    Vec3 point;
    std::int32_t distance = 0;
    std::int32_t ground_height = 0;
    std::int16_t angle = 0;       // Q.12 angle of the hit normal, 0 for a miss
    std::uint8_t ground_type = 0;
    int target = -1;              // RayTarget::id of the character hit
};

// The record of `hit` for the ray (`from`, `offset`). `previous_length` (Q.8) is what a rejected ray reports: the
// length of the last ray the game set up.
RayReport report_ray(const RayHit& hit, const Vec3& from, const Vec3& offset, std::int16_t previous_length);

}  // namespace sh::world
