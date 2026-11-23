#pragma once

// Character movement against the world: a vertical cylinder asks to move by an
// offset; walls, pillars, other characters and low ceilings stop or deflect it, and the ground under where it ends up
// is measured. Written from the behaviour of the original game; the results
// are the same integers.
//
// The pass works in two rounds. The first looks at everything near the swept cylinder: it pushes the cylinder out of
// what it already touches and finds the first thing the sweep would hit. When something stops it, that obstacle's
// ledger mark is raised (so the second round skips it), the cylinder moves up to the contact and the rest of the
// offset slides along the obstacle in a second round.

#include "engine/world/ground_query.h"
#include "engine/world/ray_query.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace sh::world {

// The moving character's cylinder (Q.12): `bottom`, `top` and `radius` relative to `position`. Y grows downwards.
struct MoveCylinder {
    Vec3 position;
    std::int16_t bottom = 0;
    std::int16_t top = 0;
    std::int16_t radius = 0;
    std::uint8_t collision_state = 0;  // higher states push lower ones aside
};

// Another character (or a part of one) in the way, Q.8 world. `mark` is its per-pass counter: the pass skips it once
// the counter is not zero, and raises it when it blocks.
struct MoveObstacle {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int16_t low = 0;           // top of its volume (the smaller Y)
    std::int16_t high = 0;          // bottom of its volume
    std::int16_t other_radius = 0;  // Q.8, its own radius
    std::uint8_t collision_state = 0;
    std::uint8_t* mark = nullptr;
};

// The obstacles of one character: its main cylinder, then extra colliders attached to it, in that order.
struct MoveBody {
    MoveObstacle main;
    std::span<MoveObstacle> parts;
};

// A character the mover slows down against when walking into it (Q.12 world, absolute heights).
struct SlowdownBody {
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::int32_t top = 0;
    std::int32_t bottom = 0;
    std::int32_t radius = 0;
    std::int32_t max_slowdown = 0;  // Q.12 fraction of the speed it may take away
};

// A low ceiling over an axis-aligned box (Q.12 world). A character whose head would rise above `height` inside the box
// is held under it and pushed off its edge.
struct CeilingBox {
    std::int32_t min_x = 0;
    std::int32_t max_x = 0;
    std::int32_t min_z = 0;
    std::int32_t max_z = 0;
    std::int32_t height = 0;
};

// What a wall face, pillar or character did to the mover in a round.
enum class MoveTouch : std::uint8_t {
    Pushed,   // touching: the mover is pushed out of it
    Near,     // just outside touching: remembered for the slide
    Stopped,  // the first thing the round's sweep hit, and the round used it
};

// The top of a wall edge as a mover is tested against it (cell space, Q.8).
struct EdgeTop {
    Point3 start;
    Point3 end;
};

// Read-only observer of a pass (the collision view). Every callback is optional and reports
// what the pass has just decided; none of them can change it.
struct MoveObserver {
    void* user = nullptr;
    void (*wall)(void* user, const ActiveCell& cell, std::uint8_t edge, const EdgeTop& top, MoveTouch touch) = nullptr;
    // `pillar` indexes CellCollision::pillars.
    void (*pillar)(void* user, const ActiveCell& cell, std::size_t pillar, MoveTouch touch) = nullptr;
    void (*body)(void* user, const MoveObstacle& body, MoveTouch touch) = nullptr;
    // The end of round `round` (0 or 1): where it started (Q.8 world), the step it took and what is left to slide
    // (Q.12), and the normal of what stopped it (zero when nothing did).
    void (*round)(void* user, int round, std::int32_t from_x, std::int32_t from_z, Vec3 step, Vec3 rest,
                  std::int32_t normal_x, std::int32_t normal_z) = nullptr;
};

struct MoveOptions {
    bool walls_to_ceiling = false;  // floors with no ground (holes) count as walls as high as the ceiling
    bool solid_pillars = true;      // pillars block (off: the mover walks through them, ground is still measured)
    // Gameplay fix: a ground rise steeper than walkable does not slow the mover down (a mis-extrapolated slope read as
    // a wall on flat floors otherwise).
    bool slope_fix = true;
    const MoveObserver* observer = nullptr;
};

struct MoveResult {
    Vec3 offset;                 // Q.12 the movement allowed (y is the asked one, held under a ceiling)
    std::int32_t ground_height = 0;  // Q.12 at the new position; 8.0 when there is no ground
    std::int16_t slope_x = 0;
    std::int16_t slope_z = 0;
    std::uint8_t ground_type = 0;
    std::int32_t ceiling_height = 0;  // Q.12, -16.0 when no ceiling is near
    // 1 when something stopped the first round, 0 when the move went through, -1 when the mover is wedged in a
    // corner (pushed from directions with no way out).
    int code = 0;

    // Diagnostics: the slope factor of the last round (Q.12, 4096 = no slowdown), whether slope_fix capped it, and
    // the contact the first round found (for the collision panel).
    std::int16_t slope_factor = 4096;
    bool slope_capped = false;
    std::int32_t contact_distance = 0;  // Q.8 distance to the last wall edge looked at
    std::int16_t radius = 0;            // Q.8 mover radius
    bool blocked = false;
    std::int16_t blocked_min = 0;       // the directions blocked, Q.12 angles
    std::int16_t blocked_max = 0;
    std::uint8_t contact_index = 0;     // the last edge or pillar looked at
};

// Moves `cylinder` by `offset`. `cells` and `bodies` are visited in order; ledgers and body marks change exactly as the
// original pass changes them.
MoveResult move_cylinder(const MoveCylinder& cylinder, Vec3 offset, std::span<const ActiveCell> cells,
                         std::span<const MoveBody> bodies, std::span<const SlowdownBody> slowdown,
                         std::span<const CeilingBox> ceilings, std::uint16_t enabled_groups,
                         const MoveOptions& options);

// The top of `edge` for a mover: its two vertices, raised where a floor beside it is a hole and holes count as walls
// (`walls_to_ceiling`). The pass tests the mover against exactly this.
EdgeTop edge_top(const CellCollision& cell, const WallEdge& edge, bool walls_to_ceiling);
// Whether a mover whose bottom is at `mover_bottom` (Q.8) is low enough for the wall under `top` to block it.
bool wall_reaches(const EdgeTop& top, std::int32_t mover_bottom);

// The ceiling part alone: clamps `offset` under the boxes and pushes it off their edges; returns the ceiling height.
std::int32_t ceiling_clamp(Vec3& offset, const MoveCylinder& cylinder, std::span<const CeilingBox> ceilings);

}  // namespace sh::world
