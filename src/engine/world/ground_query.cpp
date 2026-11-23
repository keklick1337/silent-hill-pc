#include "engine/world/ground_query.h"

#include "engine/world/fixed_math.h"

#include <algorithm>
#include <array>
#include <climits>

namespace sh::world {

namespace {

constexpr std::uint8_t kNone = 255;
constexpr std::int32_t kNoGroundDepth = 8 * 256;      // Q.8: "no ground" reads as 8 units below the origin
constexpr std::int32_t kNothingFound = 30 * 256;      // Q.8: start value of the lowest-height search
constexpr std::uint8_t kGroundTypeNothing = 12;
constexpr std::uint8_t kLedgerWrap = 252;             // the pass counter restarts after this, clearing the stamps
constexpr std::int32_t kCrossingTolerance = 6;        // Q.8

// How the probe relates to a wall edge: inside its span (the perpendicular distance counts) or past one of its ends
// (the distance to that end counts).
enum class Crossing : std::uint8_t { None = 0, Across = 1, PastEnd = 2 };

// The probe measured against one edge.
struct EdgeContact {
    Crossing crossing = Crossing::None;
    std::int32_t distance = 0;
    std::int16_t past_end = 0;  // how far beyond the end; kept from the previous edge when the probe is inside
    std::int16_t across = 0;
};

// Each floor competes in one of four slots; per slot the edge closest to the probe decides which of its two floors
// the probe stands on.
struct Slot {
    Crossing crossing = Crossing::None;
    std::uint8_t floor = kNone;
    std::int16_t past_end = 0;
    std::int32_t distance = INT_MAX;
};

bool group_enabled(std::uint16_t groups, int bit) {
    return ((groups >> bit) & 1) != 0;
}

// Whether `contact` takes the slot from the edge that holds it.
bool takes_slot(const Slot& slot, const EdgeContact& contact) {
    if (slot.crossing == Crossing::None) {
        return true;
    }
    if (contact.crossing == Crossing::Across) {
        if (slot.crossing == Crossing::Across) {
            return contact.distance < slot.distance;
        }
        return contact.distance < slot.distance + kCrossingTolerance;
    }
    if (contact.crossing == Crossing::PastEnd) {
        if (contact.distance < slot.distance - kCrossingTolerance) {
            return true;
        }
        return slot.crossing == Crossing::PastEnd && contact.distance < slot.distance + kCrossingTolerance &&
               contact.past_end < slot.past_end;
    }
    return false;
}

std::int32_t floor_height_at(const FloorPlane& f, std::int32_t local_x, std::int32_t local_z) {
    std::int32_t height = f.base_height;
    if (f.slope_x != 0) {
        height += mul_q12(f.slope_x, local_x - f.anchor_x);
    }
    if (f.slope_z != 0) {
        height += mul_q12(f.slope_z, local_z - f.anchor_z);
    }
    return height;
}

// The lowest-height search over the candidates, as the game keeps it.
struct GroundPick {
    std::int32_t height = kNothingFound;
    std::int32_t at_x = 0;  // where the height was taken (Q.8 world)
    std::int32_t at_z = 0;
    std::int16_t slope_x = 0;
    std::int16_t slope_z = 0;
    std::uint8_t height_disabled = 1;
    std::uint8_t ground_type = 0;
};

}  // namespace

GroundSample no_ground() {
    GroundSample s;
    s.height = kNoGroundDepth << 4;
    return s;
}

GroundSample sample_ground(const PlacedCell* placed, VisitLedger ledger, std::int32_t x, std::int32_t z,
                           std::uint16_t enabled_groups) {
    if (placed == nullptr || placed->cell == nullptr) {
        return no_ground();
    }
    const CellCollision& cell = *placed->cell;
    if (cell.empty() || cell.square_size == 0 || cell.squares.empty() || cell.columns == 0) {
        return no_ground();
    }

    const std::int32_t world_x = x >> 4;  // Q.8
    const std::int32_t world_z = z >> 4;
    const std::int32_t size = cell.square_size;
    const std::int32_t width = size * cell.columns;
    const std::int32_t depth = size * cell.rows;
    const std::int32_t dx = world_x - placed->origin_x;
    const std::int32_t dz = world_z - placed->origin_z;
    if (width < dx || depth < dz || dx < 0 || dz < 0) {
        return no_ground();
    }
    // The square whose lists are walked: the probe clamped into the grid.
    const auto column = static_cast<std::uint8_t>(std::clamp(dx, 0, width - 1) / size);
    const auto row = static_cast<std::uint8_t>(std::clamp(dz, 0, depth - 1) / size);

    // The cell-relative position is a 16-bit value in the game; far from the origin it wraps.
    const auto local_x = static_cast<std::int16_t>(world_x - placed->origin_x);
    const auto local_z = static_cast<std::int16_t>(world_z - placed->origin_z);
    // The square the floors are taken from: the unclamped position, divided towards zero.
    int under = -1;
    if (local_x / size >= 0 && local_x / size < cell.columns && local_z / size >= 0 && local_z / size < cell.rows) {
        under = (local_z / size) * cell.columns + local_x / size;
    }

    std::uint8_t pass = static_cast<std::uint8_t>(*ledger.pass + 1);
    if (pass > kLedgerWrap) {
        pass = 0;
        std::fill(ledger.marks.begin(), ledger.marks.end(), std::uint8_t{0});
    }

    std::array<Slot, 4> slots{};
    std::uint8_t pillar_index = under >= 0 ? kNone : 0;
    std::int32_t pillar_height = 0;
    EdgeContact contact;

    const std::size_t list = static_cast<std::size_t>(row) * cell.columns + column;
    for (int k = cell.squares[list].first; k < cell.squares[list + 1].first; ++k) {
        const std::uint8_t index = cell.edge_pool[static_cast<std::size_t>(k)];
        if (pass < ledger.marks[index]) {
            continue;
        }
        ledger.marks[index] = static_cast<std::uint8_t>(pass + 1);

        if (index < cell.edges.size()) {
            const WallEdge& edge = cell.edges[index];
            if (!group_enabled(enabled_groups, edge.group_high * 4 | edge.group_low)) {
                continue;
            }
            const Point3& start = cell.vertices[edge.start_vertex];
            const EdgeFrame frame = to_edge_frame(edge.dir_x, edge.dir_z, local_x - start.x, local_z - start.z);
            contact.across = frame.across;
            if (frame.along < 0) {
                contact.crossing = Crossing::PastEnd;
                contact.distance = planar_length(frame.along, frame.across);
                contact.past_end = static_cast<std::int16_t>(-frame.along);
            } else if (edge.length < frame.along) {
                contact.crossing = Crossing::PastEnd;
                contact.distance = planar_length(frame.along - edge.length, frame.across);
                contact.past_end = static_cast<std::int16_t>(frame.along - edge.length);
            } else {
                contact.crossing = Crossing::Across;
                contact.distance = frame.across < 0 ? -frame.across : frame.across;
            }

            if (edge.floor_left == kNone && edge.floor_right == kNone) {
                continue;
            }
            const std::uint8_t slot_floor = edge.floor_left != kNone ? edge.floor_left : edge.floor_right;
            Slot& slot = slots[cell.floors[slot_floor].side_slot];
            if (!takes_slot(slot, contact)) {
                continue;
            }
            slot.crossing = contact.crossing;
            slot.distance = contact.distance;
            slot.past_end = contact.past_end;
            const std::uint8_t side = contact.across >= 0 ? edge.floor_left : edge.floor_right;
            slot.floor = (side != kNone && cell.floors[side].height_disabled == 1) ? kNone : side;
        } else {
            const Pillar& pillar = cell.pillars[index - cell.edges.size()];
            if (!group_enabled(enabled_groups, pillar.enable_group)) {
                continue;
            }
            const std::int32_t reach = static_cast<std::int16_t>(pillar.radius) + 8;
            if (local_x < pillar.position.x - reach || pillar.position.x + reach < local_x ||
                local_z < pillar.position.z - reach || pillar.position.z + reach < local_z) {
                continue;
            }
            const auto distance =
                static_cast<std::int16_t>(planar_length(local_x - pillar.position.x, local_z - pillar.position.z));
            if (distance < pillar.radius && pillar.height_disabled != 1 &&
                (pillar_index == kNone || pillar.position.y < pillar_height)) {
                pillar_index = index;
                pillar_height = pillar.position.y;
            }
        }
    }
    *ledger.pass = static_cast<std::uint8_t>(pass + 1);

    if (under < 0) {
        return no_ground();
    }

    GroundPick pick;
    const std::int32_t here_x = local_x + placed->origin_x;
    const std::int32_t here_z = local_z + placed->origin_z;
    auto take = [&](std::int32_t height, std::int16_t slope_x, std::int16_t slope_z, std::uint8_t disabled,
                    std::uint8_t type) {
        pick.height = height;
        pick.at_x = here_x;
        pick.at_z = here_z;
        pick.slope_x = slope_x;
        pick.slope_z = slope_z;
        pick.height_disabled = disabled;
        pick.ground_type = type;
    };

    if (pillar_index != kNone && pillar_height < pick.height) {
        const Pillar& pillar = cell.pillars[pillar_index - cell.edges.size()];
        take(pillar_height, 0, 0, pillar.height_disabled, pillar.ground_type);
    }
    for (const Slot& slot : slots) {
        if (slot.floor == kNone) {
            continue;
        }
        const FloorPlane& f = cell.floors[slot.floor];
        const std::int32_t height = floor_height_at(f, local_x, local_z);
        if (height < pick.height) {
            take(height, f.slope_x, f.slope_z, f.height_disabled, f.ground_type);
        }
    }
    const int first = cell.squares[static_cast<std::size_t>(under)].first_floor;
    const int end = cell.squares[static_cast<std::size_t>(under) + 1].first_floor;
    for (int k = first; k < end; ++k) {
        const FloorPlane& f = cell.floors[cell.floor_pool[static_cast<std::size_t>(k)]];
        if (!group_enabled(enabled_groups, f.enable_group) || f.height_disabled == 1) {
            continue;
        }
        const std::int32_t height = floor_height_at(f, local_x, local_z);
        if (height < pick.height) {
            take(height, f.slope_x, f.slope_z, f.height_disabled, f.ground_type);
        }
    }

    GroundSample out;
    out.slope_x = pick.slope_x;
    out.slope_z = pick.slope_z;
    if (pick.height_disabled == 1) {
        out.height = kNoGroundDepth << 4;
        out.ground_type = 0;
        return out;
    }
    out.ground_type = static_cast<std::int8_t>(pick.ground_type);
    std::int32_t height = kNoGroundDepth;
    if (pick.ground_type != kGroundTypeNothing) {
        height = mul_q12(pick.slope_x, world_x - pick.at_x) + mul_q12(pick.slope_z, world_z - pick.at_z) + pick.height;
    }
    out.height = height << 4;
    return out;
}

}  // namespace sh::world
