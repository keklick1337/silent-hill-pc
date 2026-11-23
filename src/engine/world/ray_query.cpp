#include "engine/world/ray_query.h"

#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"
#include "engine/world/sweep.h"

#include <algorithm>
#include <array>
#include <climits>

namespace sh::world {

namespace {

constexpr std::uint8_t kNone = 255;
constexpr std::uint8_t kGroundDefault = 0;
constexpr std::uint8_t kGroundNothing = 12;
constexpr std::uint8_t kLedgerWrap = 252;
constexpr int kMaxSquares = 20;  // the walk stops once this many grid squares are collected

// The ray in the 1/256 units of the collision data, as the game sets it up.
struct Ray {
    RayMode mode = RayMode::Solid;
    std::int16_t min_pillar_radius = 0;
    std::uint16_t groups = 0;
    Vec3 from;              // Q.8 world
    std::int16_t dx = 0;    // Q.8 offset, 16-bit
    std::int16_t dy = 0;
    std::int16_t dz = 0;
    Vec3 to;                // Q.8 world
    std::int16_t length = 0;
    std::int16_t dir_x = 0; // Q.12 unit direction
    std::int16_t dir_z = 0;
    std::int16_t lowest = 0;  // the larger of the two end heights (Y grows down)
};

// The nearest contact so far.
struct Best {
    std::int16_t along = SHRT_MAX;
    Vec3 point;             // Q.8 world
    std::int16_t ground_height = 0;
    std::int16_t normal_x = 0;
    std::int16_t normal_z = 0;
    std::uint8_t ground_type = 0;
    int target = -1;
};

// The ray in cell-local 16-bit coordinates.
struct LocalRay {
    std::int32_t origin_x = 0;
    std::int32_t origin_z = 0;
    std::int16_t from_x = 0;
    std::int16_t from_z = 0;
    std::int16_t to_x = 0;
    std::int16_t to_z = 0;
};

struct Square {
    std::int16_t column = 0;
    std::int16_t row = 0;
};

bool group_enabled(std::uint16_t groups, int bit) {
    return ((groups >> bit) & 1) != 0;
}

bool open_side(const CellCollision& cell, std::uint8_t floor) {
    if (floor == kNone) {
        return true;
    }
    const std::uint8_t type = cell.floors[floor].ground_type;
    return type == kGroundDefault || type == kGroundNothing;
}

// The grid squares the ray crosses, in order, walking the major axis one square at a time (at most kMaxSquares + 1;
// the walk checks the count only between steps).
int collect_squares(const CellCollision& cell, const LocalRay& local, std::int16_t dx, std::int16_t dz,
                    std::array<Square, kMaxSquares + 2>& out) {
    int count = 0;
    const std::int32_t size = static_cast<std::int16_t>(cell.square_size);
    if (size == 0) {
        return 0;
    }
    if (local.from_x < 0 && local.to_x < 0 && local.from_z < 0 && local.to_z < 0) {
        return 0;
    }
    const auto width = static_cast<std::int16_t>(cell.columns * size);
    const auto depth = static_cast<std::int16_t>(cell.rows * size);
    if (local.from_x >= width && local.to_x >= width && local.from_z >= depth && local.to_z >= depth) {
        return 0;
    }

    // The walk runs in a mirrored frame where the ray goes towards +major, +minor with |major| >= |minor|.
    bool mirror_x = false;
    bool mirror_z = false;
    bool swap = false;
    std::int16_t start_major = local.from_x;
    std::int16_t start_minor = local.from_z;
    std::int16_t major = dx;
    std::int16_t minor = dz;
    std::int32_t major_limit = cell.columns << 12;
    std::int32_t minor_limit = cell.rows << 12;
    if (major < 0) {
        mirror_x = true;
        start_major = static_cast<std::int16_t>(-start_major);
        major = static_cast<std::int16_t>(-major);
        major_limit = 0;
    }
    if (minor < 0) {
        mirror_z = true;
        start_minor = static_cast<std::int16_t>(-start_minor);
        minor = static_cast<std::int16_t>(-minor);
        minor_limit = 0;
    }
    if (major < minor) {
        swap = true;
        std::swap(start_major, start_minor);
        std::swap(major, minor);
        std::swap(major_limit, minor_limit);
    }

    auto add = [&](std::int32_t a, std::int32_t b) {
        std::int32_t x = a;
        std::int32_t z = b;
        if (swap) {
            std::swap(x, z);
        }
        if (mirror_z) {
            z = -z;
        }
        if (mirror_x) {
            x = -x;
        }
        x >>= 12;
        z >>= 12;
        if (x >= 0 && x < cell.columns && z >= 0 && z < cell.rows) {
            out[static_cast<std::size_t>(count)] = Square{static_cast<std::int16_t>(x), static_cast<std::int16_t>(z)};
            ++count;
        }
    };

    std::int32_t end = major_limit;
    if (start_major + major < ((size * end) >> 12)) {
        end = ((start_major + major) << 12) / size;
    }
    std::int32_t at_major = (start_major << 12) / size;
    std::int32_t at_minor = (start_minor << 12) / size;
    const std::int32_t step = major != 0 ? (minor << 12) / major : 0;
    const std::int32_t corner = (step * (at_major & 0xFFF)) >> 12;
    if ((end >> 12) < (at_major >> 12)) {
        return 0;
    }
    do {
        add(at_major, at_minor);
        const std::int32_t prev_minor = at_minor;
        const std::int32_t prev_major = at_major;
        at_minor = prev_minor + step;
        at_major = prev_major + 4096;
        if ((prev_minor >> 12) < (at_minor >> 12)) {
            // The step crossed a minor line too: the square in between is the one the line was crossed in.
            if ((at_minor & 0xFFF) < corner) {
                add(at_major, prev_minor);
            } else {
                add(prev_major, at_minor);
            }
        }
    } while (count < kMaxSquares && (at_major >> 12) <= (end >> 12));
    return count;
}

void check_edge(const Ray& ray, const LocalRay& local, const CellCollision& cell, const WallEdge& edge, bool two_sided,
                Best& best) {
    const Point3& start = cell.vertices[edge.start_vertex];
    const Point3& end = cell.vertices[edge.end_vertex];
    if (!(ray.lowest >= start.y || ray.lowest >= end.y)) {
        return;
    }
    std::uint8_t ground_type = kGroundDefault;
    if (edge.floor_left != kNone) {
        ground_type = cell.floors[edge.floor_left].ground_type;
    }
    if (edge.floor_right != kNone) {
        ground_type = cell.floors[edge.floor_right].ground_type;
    }

    const EdgeFrame a = to_edge_frame(ray.dir_x, ray.dir_z, start.x - local.from_x, start.z - local.from_z);
    const EdgeFrame b = to_edge_frame(ray.dir_x, ray.dir_z, end.x - local.from_x, end.z - local.from_z);
    if ((a.across < 0) == (b.across < 0)) {
        return;
    }
    if (ray.mode == RayMode::Sight) {
        // Seen from the floor's side the edge is no obstacle.
        const std::int32_t winding = wrap_add(wrap_mul(a.along, b.across), -wrap_mul(b.along, a.across));
        if (winding >= 0 ? edge.floor_left != kNone : edge.floor_right != kNone) {
            return;
        }
    }
    if (a.across == b.across) {
        return;
    }
    const std::int32_t t = (a.across << 12) / (a.across - b.across);
    const std::int32_t along = (((b.along - a.along) * t) >> 12) + a.along;
    if (along < 0 || ray.length < along) {
        return;
    }
    const auto px = static_cast<std::int16_t>(scale_q12_16(t, end.x - start.x) + start.x);
    const auto py = static_cast<std::int16_t>(scale_q12_16(t, end.y - start.y) + start.y);
    const auto pz = static_cast<std::int16_t>(scale_q12_16(t, end.z - start.z) + start.z);
    std::int32_t height = ray.from.y;
    if (ray.dy != 0) {
        height += (ray.dy * along) / ray.length;
    }
    if (height < py || along >= best.along) {
        return;
    }
    std::int32_t normal_x = edge.dir_z;
    std::int32_t normal_z = -edge.dir_x;
    if (ray.mode != RayMode::Sight && two_sided && (b.across - a.across) > 0) {
        normal_x = -normal_x;
        normal_z = edge.dir_x;
    }
    best.along = static_cast<std::int16_t>(along);
    best.point = Vec3{px + local.origin_x, height, pz + local.origin_z};
    best.ground_height = py;
    best.normal_x = static_cast<std::int16_t>(normal_x);
    best.normal_z = static_cast<std::int16_t>(normal_z);
    best.target = -1;
    best.ground_type = ground_type;
}

void check_pillar(const Ray& ray, const LocalRay& local, const Pillar& pillar, Best& best) {
    const std::int16_t radius = pillar.radius;
    if (ray.lowest <= pillar.position.y) {
        return;
    }
    const EdgeFrame c =
        to_edge_frame(ray.dir_x, ray.dir_z, pillar.position.x - local.from_x, pillar.position.z - local.from_z);
    if (!(-radius < c.along && c.along < ray.length + radius && -radius < c.across && c.across < radius)) {
        return;
    }
    const std::int32_t half_chord = game_sqrt(radius * radius - c.across * c.across);
    const auto enter = static_cast<std::int16_t>(c.along - half_chord);
    if (enter < -half_chord || ray.length < enter || enter >= best.along) {
        return;
    }
    const std::int32_t t = (enter << 12) / ray.length;
    const std::int16_t qx = scale_q12_16(t, ray.dx);
    const std::int16_t qy = scale_q12_16(t, ray.dy);
    const std::int16_t qz = scale_q12_16(t, ray.dz);
    if (qy + ray.from.y < pillar.position.y) {
        return;
    }
    best.along = enter;
    best.point = Vec3{qx + local.from_x + local.origin_x, qy + ray.from.y, qz + local.from_z + local.origin_z};
    best.ground_height = pillar.position.y;
    best.normal_x = static_cast<std::int16_t>(qx + local.from_x - pillar.position.x);
    best.normal_z = static_cast<std::int16_t>(qz + local.from_z - pillar.position.z);
    best.target = -1;
    best.ground_type = pillar.ground_type;
}

void check_square(const Ray& ray, const LocalRay& local, const CellCollision& cell, VisitLedger ledger,
                  std::size_t square, Best& best) {
    const std::uint8_t pass = *ledger.pass;
    for (int k = cell.squares[square].first; k < cell.squares[square + 1].first; ++k) {
        const std::uint8_t index = cell.edge_pool[static_cast<std::size_t>(k)];
        if (pass < ledger.marks[index]) {
            continue;
        }
        ledger.marks[index] = static_cast<std::uint8_t>(pass + 1);
        if (index < cell.edges.size()) {
            const WallEdge& edge = cell.edges[index];
            if (!group_enabled(ray.groups, edge.group_high * 4 | edge.group_low)) {
                continue;
            }
            const bool two_sided = edge.floor_left != kNone && edge.floor_right != kNone;
            if (ray.mode == RayMode::Sight) {
                if (two_sided) {
                    continue;
                }
            } else if (open_side(cell, edge.floor_left) && open_side(cell, edge.floor_right)) {
                continue;
            }
            check_edge(ray, local, cell, edge, two_sided, best);
        } else {
            const Pillar& pillar = cell.pillars[index - cell.edges.size()];
            if (!group_enabled(ray.groups, pillar.enable_group)) {
                continue;
            }
            const bool solid = pillar.ground_type != kGroundDefault && pillar.ground_type != kGroundNothing;
            if ((ray.mode == RayMode::Sight || solid) && pillar.radius >= ray.min_pillar_radius) {
                check_pillar(ray, local, pillar, best);
            }
        }
    }
}

void check_target(const Ray& ray, const RayTarget& target, Best& best) {
    const std::int32_t min_x = std::min(ray.from.x, ray.to.x);
    const std::int32_t max_x = std::max(ray.from.x, ray.to.x);
    const std::int32_t min_z = std::min(ray.from.z, ray.to.z);
    const std::int32_t max_z = std::max(ray.from.z, ray.to.z);
    const std::int32_t reach = target.radius;
    if (target.x + reach < min_x || max_x < target.x - reach) {
        return;
    }
    if (target.z + reach < min_z || max_z < target.z - reach) {
        return;
    }
    if ((ray.from.y < target.low && ray.to.y < target.low) || (ray.from.y > target.high && target.high < ray.to.y)) {
        return;
    }
    const std::int16_t fraction =
        sweep_circle_to_point(ray.dir_x, ray.dir_z, ray.length, static_cast<std::int16_t>(target.x - ray.from.x),
                              static_cast<std::int16_t>(target.z - ray.from.z), target.radius);
    if (fraction == kNoSweepHit) {
        return;
    }
    const auto along = static_cast<std::int16_t>((ray.length * fraction) >> 12);
    if (along >= best.along) {
        return;
    }
    Vec3 p;
    p.y = ray.from.y + ((ray.dy * fraction) >> 12);
    if (p.y < target.low || target.high < p.y) {
        // The ray meets the cylinder's side outside the band: try where it crosses the cap instead.
        if (ray.dy == 0) {
            return;
        }
        std::int32_t cap;
        if (p.y < target.low) {
            cap = ((target.low - ray.from.y) << 12) / ray.dy;
            if (cap > 4096) {
                return;
            }
            p.y = target.low;
        } else {
            cap = ((target.high - ray.from.y) << 12) / ray.dy;
            if (cap > 4096) {
                return;
            }
            p.y = target.high;
        }
        p.x = ray.from.x + ((ray.dx * cap) >> 12);
        p.z = ray.from.z + ((ray.dz * cap) >> 12);
        const std::int32_t ex = target.x - p.x;
        const std::int32_t ez = target.z - p.z;
        if (wrap_add(wrap_mul(ex, ex), wrap_mul(ez, ez)) >= target.radius * target.radius) {
            return;
        }
    } else {
        p.x = ray.from.x + ((ray.dx * fraction) >> 12);
        p.z = ray.from.z + ((ray.dz * fraction) >> 12);
    }
    best.along = along;
    best.point = p;
    best.ground_height = target.low;
    best.normal_x = static_cast<std::int16_t>(p.x - target.x);
    best.normal_z = static_cast<std::int16_t>(p.z - target.z);
    best.target = target.id;
    best.ground_type = kGroundDefault;
}

}  // namespace

RayHit trace_ray(const RayRequest& request, std::span<const RayCell> cells, std::span<const RayTarget> targets) {
    RayHit out;
    if (request.offset.x == 0 && request.offset.z == 0) {
        out.rejected = true;
        return out;
    }
    Ray ray;
    ray.mode = request.mode;
    ray.min_pillar_radius = request.min_pillar_radius;
    ray.groups = request.enabled_groups;
    ray.from = Vec3{request.from.x >> 4, request.from.y >> 4, request.from.z >> 4};
    ray.dx = static_cast<std::int16_t>(request.offset.x >> 4);
    ray.dy = static_cast<std::int16_t>(request.offset.y >> 4);
    ray.dz = static_cast<std::int16_t>(request.offset.z >> 4);
    ray.to = Vec3{ray.from.x + ray.dx, ray.from.y + ray.dy, ray.from.z + ray.dz};
    ray.length = static_cast<std::int16_t>(planar_length(ray.dx, ray.dz));
    out.length = ray.length;
    if (ray.length == 0) {
        return out;
    }
    ray.dir_x = static_cast<std::int16_t>((ray.dx << 12) / ray.length);
    ray.dir_z = static_cast<std::int16_t>((ray.dz << 12) / ray.length);
    ray.lowest = static_cast<std::int16_t>(ray.dy < 0 ? ray.from.y : ray.to.y);

    Best best;
    for (const RayCell& rc : cells) {
        if (rc.placed.cell == nullptr || rc.placed.cell->empty()) {
            continue;
        }
        const CellCollision& cell = *rc.placed.cell;
        LocalRay local;
        local.origin_x = rc.placed.origin_x;
        local.origin_z = rc.placed.origin_z;
        local.from_x = static_cast<std::int16_t>(ray.from.x - local.origin_x);
        local.from_z = static_cast<std::int16_t>(ray.from.z - local.origin_z);
        local.to_x = static_cast<std::int16_t>(local.from_x + ray.dx);
        local.to_z = static_cast<std::int16_t>(local.from_z + ray.dz);

        std::array<Square, kMaxSquares + 2> squares{};
        const int count = collect_squares(cell, local, ray.dx, ray.dz, squares);

        std::uint8_t pass = static_cast<std::uint8_t>(*rc.ledger.pass + 1);
        if (pass > kLedgerWrap) {
            pass = 0;
            std::fill(rc.ledger.marks.begin(), rc.ledger.marks.end(), std::uint8_t{0});
        }
        *rc.ledger.pass = pass;
        for (int i = 0; i < count; ++i) {
            const std::size_t square =
                static_cast<std::size_t>(squares[static_cast<std::size_t>(i)].row * cell.columns +
                                         squares[static_cast<std::size_t>(i)].column);
            check_square(ray, local, cell, rc.ledger, square, best);
        }
    }
    for (const RayTarget& target : targets) {
        check_target(ray, target, best);
    }

    if (best.along == SHRT_MAX) {
        return out;
    }
    out.hit = true;
    out.point = Vec3{best.point.x << 4, best.point.y << 4, best.point.z << 4};
    out.distance = best.along << 4;
    out.ground_height = best.ground_height << 4;
    out.normal_x = best.normal_x;
    out.normal_z = best.normal_z;
    out.ground_type = best.ground_type;
    out.target = best.target;
    return out;
}

RayReport report_ray(const RayHit& hit, const Vec3& from, const Vec3& offset, std::int16_t previous_length) {
    // The game reports 1.875 as the ground height of every miss.
    constexpr std::int32_t kMissGroundHeight = 7680;
    RayReport r;
    if (!hit.hit) {
        const std::int32_t length = hit.rejected ? previous_length : hit.length;
        r.point = Vec3{wrap_add(from.x, offset.x), wrap_add(from.y, offset.y), wrap_add(from.z, offset.z)};
        r.distance = static_cast<std::int32_t>(static_cast<std::uint32_t>(length) << 4);
        r.ground_height = kMissGroundHeight;
        return r;
    }
    r.hit = true;
    r.point = hit.point;
    r.distance = hit.distance;
    r.ground_height = hit.ground_height;
    r.angle = static_cast<std::int16_t>(fixed_atan2(hit.normal_x, hit.normal_z));
    r.ground_type = hit.ground_type;
    r.target = hit.target;
    return r;
}

}  // namespace sh::world
