#include "engine/world/move_query.h"

#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"
#include "engine/world/sweep.h"

#include <algorithm>
#include <array>
#include <climits>

namespace sh::world {

namespace {

constexpr std::uint8_t kNone = 255;
constexpr std::uint8_t kGroundNothing = 12;
constexpr std::uint8_t kLedgerWrap = 252;
constexpr std::int32_t kNoGroundYet = 30 * 256;       // Q.8 start of the lowest-ground search
constexpr std::int32_t kNoGround = 8 * 256;           // Q.8
constexpr std::int32_t kNoCeiling = -16 * 4096;       // Q.12
constexpr std::int32_t kIntersectionSlack = 409;      // Q.12, 0.1 units
constexpr std::int16_t kWallToCeiling = 16 * 256;     // Q.8 how far a hole edge is raised
constexpr std::uint8_t kPassThroughState = 5;         // a collision state that ignores the world
constexpr std::int32_t kQuarterTurn = 1024;
constexpr std::int32_t kHalfTurn = 2048;

std::int32_t fract12(std::int32_t angle) {
    return angle & 0xFFF;
}

std::int32_t signed12(std::int32_t angle) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(angle) << 20) >> 20;
}

std::int32_t mul_shift12_64(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 12);
}

// How a wall edge relates to the mover: inside its span (across), or past one end.
enum class Side : std::uint8_t { None = 0, Across = 1, PastEnd = 2 };

// The floor the mover stands on per slot (see ground_query.cpp): the edge nearest to it in each of four slots says
// which side's floor counts.
struct FloorSlot {
    Side side = Side::None;
    std::uint8_t floor = kNone;
    std::int16_t past_end = 0;
    std::int32_t distance = INT_MAX;
};

// A set of blocked directions: how many obstacles pushed, and the arc of directions they leave open.
struct Arc {
    std::int8_t count = 0;
    std::int16_t from = 0;
    std::int16_t to = 0;
};

struct Walls {
    Arc blocked;                         // touching: the mover is pushed out
    std::int16_t blocked_depth = 0;
    Arc near;                            // just outside touching
    std::array<std::uint8_t*, 8> near_marks{};
    Arc bodies;                          // other characters touching
    std::int16_t body_depth = 0;
};

// The mover in the 1/256 units of the collision data.
struct Mover {
    std::uint8_t collision_state = 0;
    bool walls_to_ceiling = false;
    std::int32_t distance = 0;           // horizontal length of the offset
    std::int16_t dx = 0;                 // offset
    std::int16_t dy = 0;
    std::int16_t dz = 0;
    std::int16_t dir_x = 4096;           // Q.12 unit direction
    std::int16_t dir_z = 0;
    std::int32_t from_x = 0;             // world
    std::int32_t from_z = 0;
    std::int32_t to_x = 0;
    std::int32_t to_z = 0;
    std::int16_t radius = 0;
    std::int16_t top = 0;
    std::int16_t bottom = 0;
};

// The current edge or pillar measured against the mover.
struct Contact {
    const ActiveCell* cell = nullptr;
    std::uint8_t index = 0;
    std::uint8_t pillar_height_disabled = 0;
    // Edge: (dir_x, dir_z, length); pillar: its position.
    std::int16_t ax = 0;
    std::int16_t ay = 0;
    std::int16_t az = 0;
    std::int16_t pillar_radius = 0;
    std::uint8_t floor_left = kNone;
    std::uint8_t floor_right = kNone;
    std::uint8_t slot_left = 0;
    std::uint8_t slot_right = 0;
    std::uint8_t disabled_left = 0;
    std::uint8_t disabled_right = 0;
    Point3 start;
    Point3 end;
    // The mover in the edge's frame.
    std::int16_t start_along = 0;        // from the start vertex
    std::int16_t start_across = 0;
    std::int16_t moved_along = 0;        // the end of the move
    std::int16_t moved_across = 0;
    Side side = Side::None;
    std::int32_t distance = 0;
    std::int16_t past_end = 0;
    std::int16_t step_along = 0;         // the offset in the edge's frame
    std::int16_t step_across = 0;
};

// The first thing the sweep hits.
struct Stop {
    int kind = 0;                        // 0 nothing, 1 an edge or round obstacle, 2 an edge's end vertex
    std::int16_t fraction = 0;           // Q.12 of the move
    std::int16_t travelled = 0;          // Q.4 distance to it
    std::int16_t normal_x = 0;
    std::int16_t normal_z = 0;
    std::uint8_t* mark = nullptr;
    // What it is, for the observer: an edge or pillar of a cell (`index` as in the square lists), or a character.
    const ActiveCell* cell = nullptr;
    std::uint8_t index = 0;
    EdgeTop top;
    const MoveObstacle* body = nullptr;
};

struct State {
    bool second_round = false;
    bool moving = false;
    bool push_out = false;
    bool sample_ground = false;
    std::uint16_t groups = 0;
    Mover mover;
    Stop stop;
    Walls walls;
    // Lowest ground found.
    std::int32_t ground_height = kNoGroundYet;
    std::int32_t ground_at_x = 0;
    std::int32_t ground_at_z = 0;
    std::int32_t slope_x = 0;
    std::int32_t slope_z = 0;
    std::uint8_t height_disabled = 1;
    std::uint8_t ground_type = 0;
    // The mover in the current cell.
    std::int16_t local_from_x = 0;
    std::int16_t local_from_z = 0;
    std::int16_t local_to_x = 0;
    std::int16_t local_to_z = 0;
    // The grid squares the swept mover covers in the current cell.
    std::uint8_t first_column = 0;
    std::uint8_t first_row = 0;
    std::uint8_t columns = 0;
    std::uint8_t rows = 0;
    int square_under = -1;               // the square under the mover, -1 when outside the grid
    std::array<FloorSlot, 4> slots{};
    std::uint8_t pillar_under = 0;       // the pillar the mover stands on (kNone after entering a cell)
    std::int16_t pillar_height = 0;
    Contact contact;
    const MoveOptions* options = nullptr;
};

bool group_enabled(std::uint16_t groups, int bit) {
    return ((groups >> bit) & 1) != 0;
}

// Narrows the arc [*from, *to] to its overlap with [from2, to2] (angles turn; the arc runs counter-clockwise).
void narrow_arc(std::int16_t* from, std::int16_t* to, std::int16_t from2, std::int16_t to2) {
    const std::int32_t base = *from;
    std::int32_t width = fract12(*to - base);
    const auto start2 = static_cast<std::int16_t>(fract12(from2 - base));
    const auto end2 = static_cast<std::int16_t>(fract12(to2 - base));
    std::int32_t start = 0;
    if (start2 <= end2) {
        if (start2 > start) {
            start = start2;
        }
        if (end2 < width) {
            width = end2;
        }
        if (width < start) {
            start = width;
        }
    } else if (end2 > start) {
        if (end2 < width) {
            width = end2;
        }
    }
    *from = static_cast<std::int16_t>(fract12(start + base));
    *to = static_cast<std::int16_t>(fract12(width + base));
}

void merge_arc(Arc& arc, std::int16_t from, std::int16_t to) {
    if (arc.count == 0) {
        arc.count = 1;
        arc.from = from;
        arc.to = to;
        return;
    }
    ++arc.count;
    narrow_arc(&arc.from, &arc.to, from, to);
}

// An obstacle pushes from (dx, dz) (the mover relative to it): the open half-plane faces away from it.
// kind 0 = touching (blocks), 1 = almost touching (remembered), 2 = a character touching.
void register_push(Walls& walls, std::uint8_t* mark, int kind, std::int16_t dx, std::int16_t dz, std::int16_t depth) {
    const auto from = static_cast<std::int16_t>(fract12(fixed_atan2(dz, dx) - 1012));
    const auto to = static_cast<std::int16_t>(fract12(from + 2025));
    switch (kind) {
        case 0:
            *mark = static_cast<std::uint8_t>(*mark + 1);
            merge_arc(walls.blocked, from, to);
            if (walls.blocked_depth < depth) {
                walls.blocked_depth = depth;
            }
            break;
        case 1:
            merge_arc(walls.near, from, to);
            if (walls.near.count < 9) {
                walls.near_marks[static_cast<std::size_t>(walls.near.count - 1)] = mark;
            }
            break;
        case 2:
            *mark = static_cast<std::uint8_t>(*mark + 1);
            merge_arc(walls.bodies, from, to);
            if (walls.body_depth < depth) {
                walls.body_depth = depth;
            }
            break;
        default:
            break;
    }
}

// Whether a sweep contact at `fraction` comes before the stop found so far (kind 1: an edge, 2: a vertex; a vertex
// loses ties to an edge by 6/16 unit).
bool earlier_stop(int kind, std::int16_t fraction, const State& s) {
    std::int32_t travelled = (s.mover.distance * fraction) >> 8;
    switch (kind) {
        case 2:
            if (s.stop.kind != 1) {
                if (s.stop.kind == 0) {
                    return true;
                }
                break;
            }
            travelled += 96;
            break;
        case 1:
            if (s.stop.kind != 1) {
                if (s.stop.kind != 0) {
                    if (s.stop.kind == 2) {
                        travelled -= 96;
                    }
                    break;
                }
                return true;
            }
            break;
        default:
            return false;
    }
    return travelled < s.stop.travelled;
}

std::int16_t sweep(const State& s, std::int32_t dx, std::int32_t dz, std::int16_t radius) {
    return sweep_circle_to_point(s.mover.dir_x, s.mover.dir_z, static_cast<std::int16_t>(s.mover.distance),
                                 static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dz), radius);
}

std::uint8_t* ledger_mark(const Contact& c) {
    return &c.cell->ledger.marks[c.index];
}

void set_stop(State& s, int kind, std::int16_t fraction, std::uint8_t* mark, std::int32_t normal_x,
              std::int32_t normal_z) {
    s.stop.kind = kind;
    s.stop.fraction = fraction;
    s.stop.travelled = static_cast<std::int16_t>((s.mover.distance * fraction) >> 8);
    s.stop.mark = mark;
    s.stop.normal_x = static_cast<std::int16_t>(normal_x);
    s.stop.normal_z = static_cast<std::int16_t>(normal_z);
    s.stop.cell = s.contact.cell;
    s.stop.index = s.contact.index;
    s.stop.top = EdgeTop{s.contact.start, s.contact.end};
    s.stop.body = nullptr;
}

void tell_contact(const State& s, MoveTouch touch) {
    const MoveObserver* o = s.options->observer;
    if (o == nullptr) {
        return;
    }
    const Contact& c = s.contact;
    const std::size_t edges = c.cell->placed.cell->edges.size();
    if (c.index < edges) {
        if (o->wall != nullptr) {
            o->wall(o->user, *c.cell, c.index, EdgeTop{c.start, c.end}, touch);
        }
    } else if (o->pillar != nullptr) {
        o->pillar(o->user, *c.cell, c.index - edges, touch);
    }
}

void tell_stop(const State& s) {
    const MoveObserver* o = s.options->observer;
    if (o == nullptr) {
        return;
    }
    const Stop& stop = s.stop;
    if (stop.body != nullptr) {
        if (o->body != nullptr) {
            o->body(o->user, *stop.body, MoveTouch::Stopped);
        }
        return;
    }
    const std::size_t edges = stop.cell->placed.cell->edges.size();
    if (stop.index < edges) {
        if (o->wall != nullptr) {
            o->wall(o->user, *stop.cell, stop.index, stop.top, MoveTouch::Stopped);
        }
    } else if (o->pillar != nullptr) {
        o->pillar(o->user, *stop.cell, stop.index - edges, MoveTouch::Stopped);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Cells

// The grid squares the swept mover can touch; false when it is entirely outside the cell.
bool cover_squares(State& s, const ActiveCell& ac) {
    const CellCollision& cell = *ac.placed.cell;
    if (cell.square_size == 0) {
        return false;
    }
    const std::int32_t size = cell.square_size;
    const std::int32_t width = size * cell.columns;
    const std::int32_t depth = size * cell.rows;
    std::int32_t near_x = s.mover.from_x - ac.placed.origin_x;
    std::int32_t far_x = s.mover.to_x - ac.placed.origin_x;
    if (far_x < near_x) {
        std::swap(near_x, far_x);
    }
    near_x -= s.mover.radius;
    far_x += s.mover.radius;
    std::int32_t near_z = s.mover.from_z - ac.placed.origin_z;
    std::int32_t far_z = s.mover.to_z - ac.placed.origin_z;
    if (far_z < near_z) {
        std::swap(near_z, far_z);
    }
    near_z -= s.mover.radius;
    far_z += s.mover.radius;
    if (width < near_x || depth < near_z || far_x < 0 || far_z < 0) {
        return false;
    }
    near_x = std::clamp(near_x, 0, width - 1);
    near_z = std::clamp(near_z, 0, depth - 1);
    far_x = std::clamp(far_x, 0, width - 1);
    far_z = std::clamp(far_z, 0, depth - 1);
    s.first_column = static_cast<std::uint8_t>(near_x / size);
    s.first_row = static_cast<std::uint8_t>(near_z / size);
    s.columns = static_cast<std::uint8_t>(far_x / size - s.first_column + 1);
    s.rows = static_cast<std::uint8_t>(far_z / size - s.first_row + 1);
    return true;
}

bool enter_cell(State& s, const ActiveCell& ac) {
    if (!cover_squares(s, ac)) {
        return false;
    }
    const CellCollision& cell = *ac.placed.cell;
    s.local_from_x = static_cast<std::int16_t>(s.mover.from_x - ac.placed.origin_x);
    s.local_from_z = static_cast<std::int16_t>(s.mover.from_z - ac.placed.origin_z);
    s.local_to_x = static_cast<std::int16_t>(s.mover.to_x - ac.placed.origin_x);
    s.local_to_z = static_cast<std::int16_t>(s.mover.to_z - ac.placed.origin_z);
    const std::int32_t size = cell.square_size;
    const std::int32_t column = s.local_from_x / size;
    const std::int32_t row = s.local_from_z / size;
    if (column < 0 || column >= cell.columns || row < 0 || row >= cell.rows) {
        s.square_under = -1;
    } else {
        s.square_under = row * cell.columns + column;
        s.pillar_under = kNone;
        for (FloorSlot& slot : s.slots) {
            slot.side = Side::None;
            slot.floor = kNone;
            slot.distance = INT_MAX;
        }
    }
    return true;
}

// Measures the mover against edge `index`. False when the edge is switched off.
bool measure_edge(State& s, const ActiveCell& ac, std::uint8_t index) {
    const CellCollision& cell = *ac.placed.cell;
    const WallEdge& edge = cell.edges[index];
    if (!group_enabled(s.groups, edge.group_high * 4 | edge.group_low)) {
        return false;
    }
    Contact& c = s.contact;
    c.cell = &ac;
    c.index = index;
    c.ax = edge.dir_x;
    c.ay = edge.dir_z;
    c.az = edge.length;
    const EdgeTop top = edge_top(cell, edge, s.mover.walls_to_ceiling);
    c.start = top.start;
    c.end = top.end;
    c.floor_left = edge.floor_left;
    if (c.floor_left != kNone) {
        const FloorPlane& f = cell.floors[c.floor_left];
        c.slot_left = f.side_slot;
        c.disabled_left = f.height_disabled;
    }
    c.floor_right = edge.floor_right;
    if (c.floor_right != kNone) {
        const FloorPlane& f = cell.floors[c.floor_right];
        c.slot_right = f.side_slot;
        c.disabled_right = f.height_disabled;
    }

    const EdgeFrame step = to_edge_frame(edge.dir_x, edge.dir_z, s.mover.dx, s.mover.dz);
    c.step_along = step.along;
    c.step_across = step.across;
    const EdgeFrame at = to_edge_frame(edge.dir_x, edge.dir_z, s.local_from_x - c.start.x, s.local_from_z - c.start.z);
    c.start_along = at.along;
    c.start_across = at.across;
    c.moved_along = static_cast<std::int16_t>(c.step_along + at.along);
    c.moved_across = static_cast<std::int16_t>(c.step_across + at.across);
    if (at.along < 0) {
        c.side = Side::PastEnd;
        c.distance = planar_length(at.along, at.across);
        c.past_end = static_cast<std::int16_t>(-at.along);
    } else if (c.az < at.along) {
        c.side = Side::PastEnd;
        c.distance = planar_length(at.along - c.az, at.across);
        c.past_end = static_cast<std::int16_t>(at.along - c.az);
    } else {
        c.distance = at.across < 0 ? -at.across : at.across;
        c.side = Side::Across;
    }
    return true;
}

// Whether the measured edge takes a floor slot from the one that holds it.
bool takes_slot(const FloorSlot& slot, const Contact& c) {
    if (slot.side == Side::None) {
        return true;
    }
    switch (c.side) {
        case Side::None:
            return false;
        case Side::Across:
            if (slot.side == Side::Across) {
                return c.distance < slot.distance;
            }
            if (slot.side == Side::PastEnd) {
                return c.distance < slot.distance + 6;
            }
            return false;
        case Side::PastEnd:
            if (slot.side == Side::Across) {
                return c.distance < slot.distance - 6;
            }
            if (slot.side == Side::PastEnd) {
                if (c.distance < slot.distance - 6) {
                    return true;
                }
                return c.distance < slot.distance + 6 && c.past_end < slot.past_end;
            }
            return false;
    }
    return false;
}

void claim_floor_slot(State& s) {
    const Contact& c = s.contact;
    std::uint8_t slot_index;
    if (c.floor_left == kNone) {
        if (c.floor_right == kNone) {
            return;
        }
        slot_index = c.slot_right;
    } else {
        slot_index = c.slot_left;
    }
    FloorSlot& slot = s.slots[slot_index];
    if (!takes_slot(slot, c)) {
        return;
    }
    slot.side = c.side;
    slot.distance = c.distance;
    slot.past_end = c.past_end;
    if (c.start_across >= 0) {
        slot.floor = c.disabled_left == 1 ? kNone : c.floor_left;
    } else {
        slot.floor = c.disabled_right == 1 ? kNone : c.floor_right;
    }
}

// Mirrors the measured edge so that its open side is the right one.
void flip_contact(Contact& c) {
    std::swap(c.start, c.end);
    c.ax = static_cast<std::int16_t>(-c.ax);
    c.ay = static_cast<std::int16_t>(-c.ay);
    std::swap(c.floor_left, c.floor_right);
    std::swap(c.slot_left, c.slot_right);
    c.step_along = static_cast<std::int16_t>(-c.step_along);
    c.step_across = static_cast<std::int16_t>(-c.step_across);
    c.start_across = static_cast<std::int16_t>(-c.start_across);
    c.start_along = static_cast<std::int16_t>(c.az - c.start_along);
    c.moved_across = static_cast<std::int16_t>(-c.moved_across);
    c.moved_along = static_cast<std::int16_t>(c.az - c.moved_along);
}

// How far the mover's bottom is below the edge's top at the nearest point.
std::int32_t height_over_edge(const State& s) {
    const Contact& c = s.contact;
    const std::int16_t along = c.start_along;
    std::int16_t top;
    if (along < 0) {
        top = c.start.y;
    } else if (c.az < along) {
        top = c.end.y;
    } else if (c.start.y == c.end.y) {
        top = c.start.y;
    } else {
        top = static_cast<std::int16_t>(c.start.y + ((c.end.y - c.start.y) * along) / c.az);
    }
    return s.mover.bottom - top;
}

void push_from_edge(State& s, int kind) {
    if (height_over_edge(s) < 0) {
        return;
    }
    const Contact& c = s.contact;
    std::int32_t dx;
    std::int32_t dz;
    if (c.start_along < 0) {
        dx = s.local_from_x - c.start.x;
        dz = s.local_from_z - c.start.z;
    } else if (c.az < c.start_along) {
        dx = s.local_from_x - c.end.x;
        dz = s.local_from_z - c.end.z;
    } else {
        dx = c.ay;
        dz = -c.ax;
    }
    register_push(s.walls, ledger_mark(c), kind, static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dz),
                  static_cast<std::int16_t>(s.mover.radius - c.distance));
    tell_contact(s, kind == 0 ? MoveTouch::Pushed : MoveTouch::Near);
}

void sweep_to_vertex(State& s, const Point3& v) {
    const std::int16_t t = sweep(s, v.x - s.local_from_x, v.z - s.local_from_z, s.mover.radius);
    if (t != kNoSweepHit && earlier_stop(2, t, s) && s.mover.bottom > v.y) {
        const std::int32_t x = s.local_from_x + ((s.mover.dx * t) >> 12);
        const std::int32_t z = s.local_from_z + ((s.mover.dz * t) >> 12);
        set_stop(s, 2, t, ledger_mark(s.contact), x - v.x, z - v.z);
    }
}

void sweep_to_edge(State& s, std::int16_t fraction, std::int16_t along) {
    const Contact& c = s.contact;
    if (!earlier_stop(1, fraction, s) || c.step_across < 0) {
        return;
    }
    const std::int32_t rise = ((c.end.y - c.start.y) * along) / c.az;
    if (rise + c.start.y < s.mover.bottom) {
        set_stop(s, 1, fraction, ledger_mark(c), c.ay, -c.ax);
    }
}

void sweep_against_edge(State& s) {
    const Contact& c = s.contact;
    const std::int32_t reach = -s.mover.radius;
    std::int32_t fraction = 0;
    std::int32_t along = 0;
    int target;
    if (c.start_across >= reach) {
        if (c.start_along >= 0) {
            if (c.az >= c.start_along) {
                along = c.start_along;
                target = 2;
            } else {
                target = 1;
            }
        } else {
            target = 0;
        }
    } else {
        if (c.step_across == 0) {
            if (c.start_along > 0 && c.start_along < c.az) {
                fraction = 0;
                along = c.start_along;
            }
        } else {
            fraction = ((reach - c.start_across) << 12) / c.step_across;
            along = ((c.step_along * fraction) >> 12) + c.start_along;
        }
        if (along < 0) {
            target = 0;
        } else if (c.az < along) {
            target = 1;
        } else {
            target = 2;
        }
    }
    switch (target) {
        case 0:
            sweep_to_vertex(s, c.start);
            break;
        case 1:
            sweep_to_vertex(s, c.end);
            break;
        default:
            sweep_to_edge(s, static_cast<std::int16_t>(fraction), static_cast<std::int16_t>(along));
            break;
    }
}

// The measured edge has its wall face towards the mover's side.
void check_wall(State& s) {
    const Contact& c = s.contact;
    if (!(c.floor_right == kNone && c.start_across < 0 && wall_reaches(EdgeTop{c.start, c.end}, s.mover.bottom))) {
        return;
    }
    if (s.push_out && c.distance < s.mover.radius) {
        push_from_edge(s, 0);
        return;
    }
    const std::int32_t r = s.mover.radius;
    if (s.push_out && c.distance < r + 8) {
        push_from_edge(s, 1);
    }
    if (s.moving && s.walls.blocked.count == 0 && (-r < c.start_across || -r < c.moved_across) &&
        (-r < c.start_along || -r < c.moved_along) && (c.start_along < r + c.az || c.moved_along < r + c.az)) {
        sweep_against_edge(s);
    }
}

bool measure_pillar(State& s, const ActiveCell& ac, std::uint8_t index) {
    const CellCollision& cell = *ac.placed.cell;
    Contact& c = s.contact;
    c.cell = &ac;
    c.index = index;
    const Pillar& p = cell.pillars[index - cell.edges.size()];
    if (!group_enabled(s.groups, p.enable_group)) {
        return false;
    }
    c.pillar_height_disabled = p.height_disabled;
    c.ax = p.position.x;
    c.ay = p.position.y;
    c.az = p.position.z;
    c.pillar_radius = p.radius;
    return true;
}

void push_from_pillar(State& s, int kind, std::int32_t distance) {
    const Contact& c = s.contact;
    if (s.mover.bottom >= c.ay + (distance - c.pillar_radius)) {
        register_push(s.walls, ledger_mark(c), kind, static_cast<std::int16_t>(s.local_from_x - c.ax),
                      static_cast<std::int16_t>(s.local_from_z - c.az),
                      static_cast<std::int16_t>(s.mover.radius + c.pillar_radius - distance));
        tell_contact(s, kind == 0 ? MoveTouch::Pushed : MoveTouch::Near);
    }
}

void check_pillar(State& s) {
    const Contact& c = s.contact;
    const auto touch = static_cast<std::int16_t>(s.mover.radius + c.pillar_radius);
    const std::int32_t bound = touch + 8;
    if (s.local_from_x < c.ax - bound && s.local_to_x < c.ax - bound) {
        return;
    }
    if (c.ax + bound < s.local_from_x && c.ax + bound < s.local_to_x) {
        return;
    }
    if (s.local_from_z < c.az - bound && s.local_to_z < c.az - bound) {
        return;
    }
    if (c.az + bound < s.local_from_z && c.az + bound < s.local_to_z) {
        return;
    }
    const auto distance =
        static_cast<std::int16_t>(planar_length(s.local_from_x - c.ax, s.local_from_z - c.az));
    if (distance < c.pillar_radius && c.pillar_height_disabled != 1 &&
        (s.pillar_under == kNone || c.ay < s.pillar_height)) {
        s.pillar_under = c.index;
        s.pillar_height = c.ay;
    }
    if ((!s.moving && !s.push_out) || distance < c.pillar_radius) {
        return;
    }
    if (distance < touch && s.push_out) {
        push_from_pillar(s, 0, distance);
        return;
    }
    if (distance < touch + 8 && s.push_out) {
        push_from_pillar(s, 1, distance);
    }
    if (!s.moving || s.walls.blocked.count != 0) {
        return;
    }
    std::int16_t t = sweep(s, c.ax - s.local_from_x, c.az - s.local_from_z, touch);
    if (t == kNoSweepHit) {
        return;
    }
    if (t < 0) {
        t = 0;
    }
    if (earlier_stop(1, t, s) && s.mover.bottom >= c.ay) {
        const std::int32_t x = s.local_from_x + ((s.mover.dx * t) >> 12);
        const std::int32_t z = s.local_from_z + ((s.mover.dz * t) >> 12);
        set_stop(s, 1, t, ledger_mark(c), x - c.ax, z - c.az);
    }
}

void visit_square(State& s, const ActiveCell& ac, std::size_t square) {
    const CellCollision& cell = *ac.placed.cell;
    const VisitLedger& ledger = ac.ledger;
    for (int k = cell.squares[square].first; k < cell.squares[square + 1].first; ++k) {
        const std::uint8_t index = cell.edge_pool[static_cast<std::size_t>(k)];
        if (*ledger.pass < ledger.marks[index]) {
            continue;
        }
        ledger.marks[index] = static_cast<std::uint8_t>(*ledger.pass + 1);
        if (index < cell.edges.size()) {
            if (!measure_edge(s, ac, index)) {
                continue;
            }
            if (s.sample_ground) {
                claim_floor_slot(s);
            }
            if (s.moving || s.push_out) {
                if (s.contact.floor_right == kNone) {
                    check_wall(s);
                }
                if (s.contact.floor_left == kNone) {
                    flip_contact(s.contact);
                    check_wall(s);
                }
            }
        } else if (measure_pillar(s, ac, index)) {
            check_pillar(s);
        }
    }
}

std::int32_t floor_height_at(const FloorPlane& f, std::int32_t x, std::int32_t z) {
    std::int32_t height = f.base_height;
    if (f.slope_x != 0) {
        height += mul_q12(f.slope_x, x - f.anchor_x);
    }
    if (f.slope_z != 0) {
        height += mul_q12(f.slope_z, z - f.anchor_z);
    }
    return height;
}

void offer_ground(State& s, const ActiveCell& ac, std::int32_t height, std::int32_t slope_x, std::int32_t slope_z,
                  std::uint8_t disabled, std::uint8_t type) {
    s.ground_height = height;
    s.ground_at_x = s.local_from_x + ac.placed.origin_x;
    s.ground_at_z = s.local_from_z + ac.placed.origin_z;
    s.slope_x = slope_x;
    s.slope_z = slope_z;
    s.height_disabled = disabled;
    s.ground_type = type;
}

void measure_ground(State& s, const ActiveCell& ac) {
    if (s.square_under < 0) {
        return;
    }
    const CellCollision& cell = *ac.placed.cell;
    if (s.pillar_under != kNone && s.pillar_height < s.ground_height) {
        const Pillar& p = cell.pillars[s.pillar_under - cell.edges.size()];
        offer_ground(s, ac, s.pillar_height, 0, 0, p.height_disabled, p.ground_type);
    }
    for (const FloorSlot& slot : s.slots) {
        if (slot.floor == kNone) {
            continue;
        }
        const FloorPlane& f = cell.floors[slot.floor];
        const std::int32_t height = floor_height_at(f, s.local_from_x, s.local_from_z);
        if (height < s.ground_height) {
            offer_ground(s, ac, height, f.slope_x, f.slope_z, f.height_disabled, f.ground_type);
        }
    }
    const auto square = static_cast<std::size_t>(s.square_under);
    const int first = cell.squares[square].first_floor;
    const int end = cell.squares[square + 1].first_floor;
    for (int k = first; k < end; ++k) {
        const FloorPlane& f = cell.floors[cell.floor_pool[static_cast<std::size_t>(k)]];
        if (!group_enabled(s.groups, f.enable_group) || f.height_disabled == 1) {
            continue;
        }
        const std::int32_t height = floor_height_at(f, s.local_from_x, s.local_from_z);
        if (height < s.ground_height) {
            offer_ground(s, ac, height, f.slope_x, f.slope_z, f.height_disabled, f.ground_type);
        }
    }
}

void visit_cell(State& s, const ActiveCell& ac) {
    const CellCollision& cell = *ac.placed.cell;
    if (cell.empty() || !enter_cell(s, ac)) {
        return;
    }
    if (cell.squares.empty() || cell.columns == 0) {
        return;
    }
    const VisitLedger& ledger = ac.ledger;
    if (!s.second_round) {
        std::uint8_t pass = static_cast<std::uint8_t>(*ledger.pass + 1);
        if (pass > kLedgerWrap) {
            pass = 0;
            std::fill(ledger.marks.begin(), ledger.marks.end(), std::uint8_t{0});
        }
        *ledger.pass = pass;
    }
    const int last_column = s.first_column + s.columns - 1;
    if (last_column < cell.columns && s.first_row + s.rows <= cell.rows) {
        for (int row = s.first_row; row < s.first_row + s.rows; ++row) {
            for (int column = s.first_column; column <= last_column; ++column) {
                visit_square(s, ac, static_cast<std::size_t>(row * cell.columns + column));
            }
        }
    }
    if (!s.second_round) {
        *ledger.pass = static_cast<std::uint8_t>(*ledger.pass + 1);
    }
    if (s.sample_ground) {
        measure_ground(s, ac);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Characters

void check_obstacle(State& s, const MoveObstacle& ob, std::int16_t reach) {
    if (ob.collision_state < 2 || *ob.mark != 0) {
        return;
    }
    const Mover& m = s.mover;
    const std::int32_t span = reach + m.distance;
    if (ob.x + span < m.from_x || m.to_x < ob.x - span) {
        return;
    }
    if (ob.z + span < m.from_z || m.to_z < ob.z - span || m.top > ob.high) {
        return;
    }
    const std::int32_t dx = m.from_x - ob.x;
    if (m.bottom < ob.low) {
        return;
    }
    const std::int32_t dz = m.from_z - ob.z;
    const std::int32_t distance = planar_length(dx, dz);
    const std::int16_t t = sweep(s, ob.x - m.from_x, ob.z - m.from_z, reach);
    if (t == kNoSweepHit) {
        return;
    }
    if (t == 0) {
        if (s.push_out) {
            register_push(s.walls, ob.mark, 2, static_cast<std::int16_t>(dx), static_cast<std::int16_t>(dz),
                          static_cast<std::int16_t>(reach - distance));
            if (s.options->observer != nullptr && s.options->observer->body != nullptr) {
                s.options->observer->body(s.options->observer->user, ob, MoveTouch::Pushed);
            }
        }
    } else if (s.moving && s.walls.blocked.count == 0 && earlier_stop(1, t, s)) {
        set_stop(s, 1, t, ob.mark, dx + ((t * m.dx) >> 12), dz + ((t * m.dz) >> 12));
        s.stop.body = &ob;
    }
}

void visit_obstacle(State& s, const MoveObstacle& ob) {
    std::int32_t reach = ob.other_radius + s.mover.radius;
    if (ob.collision_state < s.mover.collision_state) {
        reach -= 15;
    }
    if (!s.second_round) {
        *ob.mark = 0;
    }
    check_obstacle(s, ob, static_cast<std::int16_t>(reach));
}

// ---------------------------------------------------------------------------------------------------------------
// Resolving the move

std::int32_t ground_at(const State& s, std::int32_t x, std::int32_t z) {
    if (s.ground_type != kGroundNothing) {
        return mul_q12(s.slope_x, x - s.ground_at_x) + mul_q12(s.slope_z, z - s.ground_at_z) + s.ground_height;
    }
    return kNoGround;
}

// How much of the move a rising slope leaves (Q.12).
std::int16_t slope_factor(const State& s, MoveResult& out) {
    out.slope_factor = 4096;
    if (s.ground_height == kNoGroundYet) {
        return 4096;
    }
    const std::int32_t ahead = ground_at(s, s.mover.to_x, s.mover.to_z);
    if (s.mover.bottom + s.mover.dy < ahead || ahead == s.ground_height) {
        return 4096;
    }
    const std::int32_t length = planar_length(s.mover.distance, ahead - s.mover.bottom);
    if (length == 0) {
        return 4096;
    }
    const auto factor = static_cast<std::int16_t>((s.mover.distance << 12) / length);
    out.slope_factor = factor;
    if (s.options->slope_fix && factor < 3276) {
        out.slope_capped = true;
        return 4096;
    }
    return factor;
}

// Keeps `pos` inside the open arc [from, to] around a pivot `distance` along `angle`.
void clamp_to_sector(Vec3& pos, std::int32_t angle, std::int32_t from, std::int32_t to, std::int32_t distance) {
    const std::int32_t heading = fract12(angle);
    const std::int32_t low = fract12(from);
    const std::int32_t high_in = fract12(to);
    if (distance > 256) {
        distance = 256;
    }
    const std::int32_t px = mul_q12(distance, fixed_cos(heading));
    std::int32_t high = high_in;
    const std::int32_t pz = mul_q12(distance, fixed_sin(heading));
    const std::int32_t dx = pos.x - px;
    const std::int32_t dz = pos.z - pz;
    std::int32_t direction = fract12(fixed_atan2(dz, dx));
    if (direction < low) {
        direction += 4096;
    }
    if (high < low) {
        high += 4096;
    }
    const std::int32_t wrapped_low = low + 4096;
    if (high < direction) {
        if (((high + wrapped_low) >> 1) < direction) {
            high = wrapped_low;
        }
        const std::int32_t s = fixed_sin(high);
        const std::int32_t c = fixed_cos(high);
        std::int32_t along = mul_q12(dx, c) + mul_q12(dz, s);
        if (along < 0) {
            along = 0;
        }
        pos.x = px + mul_q12(along, c);
        pos.z = pz + mul_q12(along, s);
    }
}

void clamp_to_open_arc(Vec3& rest, Walls& w) {
    if (w.bodies.count != 0) {
        bool none = false;
        std::int16_t from = 0;
        std::int16_t to = 0;
        if (w.blocked.count != 0) {
            from = w.blocked.from;
            to = w.blocked.to;
            if (w.near.count != 0) {
                narrow_arc(&from, &to, w.near.from, w.near.to);
            }
        } else if (w.near.count != 0) {
            from = w.near.from;
            to = w.near.to;
        } else {
            none = true;
        }
        bool take_bodies;
        if (none) {
            take_bodies = true;
        } else {
            const auto heading = static_cast<std::int16_t>(fixed_atan2(rest.z, rest.x));
            const auto a = static_cast<std::int16_t>(signed12(to - (heading - kQuarterTurn)));
            const auto b = static_cast<std::int16_t>(signed12(from - (heading + kQuarterTurn)));
            take_bodies = !(a < 0 && b > 0);
        }
        if (take_bodies) {
            if (w.blocked.count == 0) {
                w.blocked = w.bodies;
                w.blocked_depth = w.body_depth;
            } else {
                w.blocked.count = static_cast<std::int8_t>(w.blocked.count + w.bodies.count);
                narrow_arc(&w.blocked.from, &w.blocked.to, w.bodies.from, w.bodies.to);
                w.blocked_depth = std::max(w.blocked_depth, w.body_depth);
            }
        }
        if (w.near.count != 0) {
            if (w.blocked.count == 0) {
                w.blocked = w.near;
                w.blocked_depth = 0;
            } else {
                w.blocked.count = static_cast<std::int8_t>(w.blocked.count + w.near.count);
                narrow_arc(&w.blocked.from, &w.blocked.to, w.near.from, w.near.to);
            }
        }
    }

    if (w.blocked.count == 0) {
        return;
    }
    if (w.blocked.from == w.blocked.to) {
        rest.x = 0;
        rest.z = 0;
        return;
    }
    auto middle = static_cast<std::int16_t>((w.blocked.from + w.blocked.to) >> 1);
    if (w.blocked.to < w.blocked.from) {
        middle = static_cast<std::int16_t>(fract12(middle + kHalfTurn));
    }
    std::int16_t from;
    std::int16_t to;
    if (w.near.count == 0) {
        from = w.blocked.from;
        to = w.blocked.to;
    } else {
        from = w.blocked.from;
        to = w.blocked.to;
        narrow_arc(&from, &to, w.near.from, w.near.to);
        from = static_cast<std::int16_t>(fract12(from));
        to = static_cast<std::int16_t>(fract12(to));
        if (from == to) {
            from = w.blocked.from;
            to = w.blocked.to;
        }
        if (from != to) {
            const std::int32_t to_from = signed12(from - middle);
            const std::int32_t to_to = signed12(to - middle);
            if (to_from >= 0 || to_to <= 0) {
                middle = (to_from < 0 ? -to_from : to_from) < (to_to < 0 ? -to_to : to_to) ? from : to;
            }
        }
    }
    const auto pivot = static_cast<std::int16_t>(std::min(w.blocked_depth + 2, 16) * 16);
    clamp_to_sector(rest, middle, from, to, pivot);
}

// Splits the remaining offset into the step taken now and what is left to slide.
void resolve(Vec3& step, Vec3& rest, std::int16_t factor, State& s) {
    const Vec3 scaled{mul_q12(rest.x, factor), 0, mul_q12(rest.z, factor)};
    if (s.walls.blocked.count != 0 || s.walls.bodies.count != 0) {
        step.x = 0;
        step.z = 0;
        rest.x = scaled.x;
        rest.z = scaled.z;
        clamp_to_open_arc(rest, s.walls);
        return;
    }
    if (s.stop.kind == 0) {
        step.x = scaled.x;
        step.z = scaled.z;
        rest.x = 0;
        rest.z = 0;
        return;
    }
    if (factor < s.stop.fraction) {
        s.stop.kind = 0;
        step.x = scaled.x;
        step.z = scaled.z;
        rest.x = 0;
        rest.z = 0;
        return;
    }
    step.x = mul_q12(rest.x, s.stop.fraction);
    step.z = mul_q12(rest.z, s.stop.fraction);
    rest.x = scaled.x - step.x;
    rest.z = scaled.z - step.z;

    std::int32_t nx = s.stop.normal_x;
    std::int32_t nz = s.stop.normal_z;
    const std::int32_t square = nx * nx + nz * nz;
    if (square < 256) {
        const std::int32_t length = game_sqrt(square * 256);
        nx = length != 0 ? (nx << 16) / length : 0;
        nz = length != 0 ? (nz << 16) / length : 0;
    } else {
        const std::int32_t length = game_sqrt(square);
        nx = (nx << 12) / length;
        nz = (nz << 12) / length;
    }
    // Slide: keep the part of the rest along the obstacle's tangent.
    const std::int32_t along = (rest.x * nz + rest.z * -nx) >> 12;
    rest.x = mul_q12(along, nz);
    rest.z = mul_q12(along, -nx);
    // Step a hair off the obstacle so the next round does not start inside it.
    if (nx > 1365) {
        step.x += 16;
    } else if (nx < -1365) {
        step.x -= 16;
    }
    if (nz > 1365) {
        step.z += 16;
    } else if (nz < -1365) {
        step.z -= 16;
    }
}

void aim(Mover& m) {
    m.distance = planar_length(m.dx, m.dz);
    if (m.distance != 0) {
        const std::int32_t heading = fixed_atan2(m.dz, m.dx);
        m.dir_x = static_cast<std::int16_t>(fixed_cos(heading));
        m.dir_z = static_cast<std::int16_t>(fixed_sin(heading));
    } else {
        m.dir_x = 4096;
        m.dir_z = 0;
    }
}

void start_second_round(State& s, const Vec3& step, const Vec3& rest) {
    const auto step_x = static_cast<std::int16_t>(step.x >> 4);
    const auto step_z = static_cast<std::int16_t>(step.z >> 4);
    s.stop.kind = 0;
    s.walls.blocked.count = 0;
    s.walls.blocked_depth = 0;
    s.walls.near.count = 0;
    s.walls.body_depth = 0;
    s.walls.bodies.count = 0;
    Mover& m = s.mover;
    m.dx = static_cast<std::int16_t>(rest.x >> 4);
    m.dz = static_cast<std::int16_t>(rest.z >> 4);
    aim(m);
    m.from_x += step_x;
    m.from_z += step_z;
    m.to_x = m.from_x + m.dx;
    m.to_z = m.from_z + m.dz;
}

void slow_down(Vec3& offset, const MoveCylinder& cylinder, std::span<const SlowdownBody> bodies) {
    std::int32_t keep = 4096;
    const std::int32_t heading = fixed_atan2(offset.x, offset.z);
    for (const SlowdownBody& b : bodies) {
        if (b.top > cylinder.bottom + cylinder.position.y || b.bottom < cylinder.top + cylinder.position.y) {
            continue;
        }
        const std::int32_t dx = b.x - cylinder.position.x;
        const std::int32_t dz = b.z - cylinder.position.z;
        const std::int32_t distance = fixed_length3(dx, 0, dz);
        if (b.radius + cylinder.radius + kIntersectionSlack < distance) {
            continue;
        }
        std::int32_t loss = mul_q12(fixed_cos(fixed_atan2(dx, dz) - heading), 6144);
        loss = std::max(loss, 0);
        loss = std::min(loss, b.max_slowdown);
        keep -= loss;
    }
    keep = std::max(keep, 1638);
    offset.x = mul_q12(keep, offset.x);
    offset.z = mul_q12(keep, offset.z);
}

// The point of `box` nearest to (x, z), as an offset from it (0 inside).
void offset_to_box(std::int32_t& ox, std::int32_t& oz, std::int32_t x, std::int32_t z, const CeilingBox& box) {
    if (x < box.min_x) {
        ox = x - box.min_x;
    } else if (box.max_x >= x) {
        ox = 0;
    } else {
        ox = x - box.max_x;
    }
    if (z < box.min_z) {
        oz = z - box.min_z;
    } else if (box.max_z >= z) {
        oz = 0;
    } else {
        oz = z - box.max_z;
    }
}

}  // namespace

EdgeTop edge_top(const CellCollision& cell, const WallEdge& edge, bool walls_to_ceiling) {
    EdgeTop top{cell.vertices[edge.start_vertex], cell.vertices[edge.end_vertex]};
    if (!walls_to_ceiling) {
        return top;
    }
    const auto hole = [&cell](std::uint8_t floor) {
        if (floor == kNone) {
            return false;
        }
        const FloorPlane& f = cell.floors[floor];
        return f.height_disabled == 1 || f.ground_type == kGroundNothing;
    };
    if (hole(edge.floor_left)) {
        top.start.y = static_cast<std::int16_t>(top.start.y - kWallToCeiling);
        top.end.y = static_cast<std::int16_t>(top.end.y - kWallToCeiling);
    }
    if (hole(edge.floor_right)) {
        top.start.y = -kWallToCeiling;
        top.end.y = -kWallToCeiling;
    }
    return top;
}

bool wall_reaches(const EdgeTop& top, std::int32_t mover_bottom) {
    return mover_bottom >= top.start.y || mover_bottom >= top.end.y;
}

std::int32_t ceiling_clamp(Vec3& offset, const MoveCylinder& cylinder, std::span<const CeilingBox> ceilings) {
    const std::int32_t radius = cylinder.radius;
    std::int32_t ceiling = kNoCeiling;
    std::int32_t push_x = 0;
    std::int32_t push_z = 0;
    std::int32_t move_x = offset.x;
    std::int32_t move_z = offset.z;
    const std::int32_t head = cylinder.position.y + cylinder.top;
    const std::int32_t head_after = head + offset.y;
    for (const CeilingBox& box : ceilings) {
        if (head_after - box.height >= 0) {
            continue;
        }
        std::int32_t ox;
        std::int32_t oz;
        offset_to_box(ox, oz, cylinder.position.x + move_x, cylinder.position.z + move_z, box);
        if (std::max(ox < 0 ? -ox : ox, oz < 0 ? -oz : oz) >= radius) {
            continue;
        }
        const std::int32_t gap_after = fixed_length3(ox, 0, oz);
        if (gap_after >= radius) {
            continue;
        }
        if (gap_after > 0) {
            std::int32_t ox0;
            std::int32_t oz0;
            offset_to_box(ox0, oz0, cylinder.position.x, cylinder.position.z, box);
            std::int32_t allowed = kIntersectionSlack;
            const std::int32_t gap = fixed_length3(ox0, 0, oz0);
            if (radius - gap <= kIntersectionSlack) {
                allowed = radius - gap;
            }
            if (gap_after - gap < allowed) {
                const std::int32_t angle = fixed_atan2(ox, oz);
                const std::int32_t push = allowed - (gap_after - gap);
                push_x = mul_shift12_64(push, fixed_sin(angle));
                push_z = mul_shift12_64(push, fixed_cos(angle));
            }
        } else {
            if (box.height < ceiling) {
                ceiling = box.height;
            }
            move_x = offset.x;
            move_z = offset.z;
            break;
        }
        move_x += push_x;
        move_z += push_z;
    }
    offset.x = move_x;
    offset.z = move_z;
    if (ceiling != kNoCeiling) {
        std::int32_t headroom = kIntersectionSlack;
        if (ceiling - head < kIntersectionSlack) {
            headroom = ceiling - head;
        }
        if (offset.y < headroom) {
            offset.y = headroom;
        }
    }
    return ceiling;
}

MoveResult move_cylinder(const MoveCylinder& cylinder, Vec3 offset, std::span<const ActiveCell> cells,
                         std::span<const MoveBody> bodies, std::span<const SlowdownBody> slowdown,
                         std::span<const CeilingBox> ceilings, std::uint16_t enabled_groups,
                         const MoveOptions& options) {
    MoveResult out;
    if (cylinder.collision_state == kPassThroughState) {
        out.offset = offset;
        out.ground_height = cylinder.position.y;
        out.ceiling_height = kNoCeiling;
        out.code = 0;
        return out;
    }
    slow_down(offset, cylinder, slowdown);
    Vec3 asked = offset;
    out.ceiling_height = ceiling_clamp(asked, cylinder, ceilings);

    State s;
    s.options = &options;
    s.groups = enabled_groups;
    Mover& m = s.mover;
    m.walls_to_ceiling = options.walls_to_ceiling;
    m.dx = static_cast<std::int16_t>(asked.x >> 4);
    m.dy = static_cast<std::int16_t>(asked.y >> 4);
    m.dz = static_cast<std::int16_t>(asked.z >> 4);
    aim(m);
    m.radius = static_cast<std::int16_t>(cylinder.radius >> 4);
    m.from_x = cylinder.position.x >> 4;
    m.from_z = cylinder.position.z >> 4;
    m.to_x = m.from_x + m.dx;
    m.to_z = m.from_z + m.dz;
    m.top = static_cast<std::int16_t>((cylinder.top + cylinder.position.y) >> 4);
    m.bottom = static_cast<std::int16_t>((cylinder.bottom + cylinder.position.y) >> 4);
    m.collision_state = cylinder.collision_state;

    Vec3 rest = asked;
    Vec3 step;
    out.offset = Vec3{0, asked.y, 0};
    bool corner = false;
    while (true) {
        s.moving = m.distance != 0;
        s.push_out = !s.second_round;
        s.sample_ground = !s.second_round;
        for (const ActiveCell& cell : cells) {
            visit_cell(s, cell);
        }
        if (s.walls.blocked.count != 0 && s.walls.blocked.from == s.walls.blocked.to) {
            corner = true;
        }
        for (const MoveBody& body : bodies) {
            visit_obstacle(s, body.main);
            for (const MoveObstacle& part : body.parts) {
                visit_obstacle(s, part);
            }
        }
        resolve(step, rest, slope_factor(s, out), s);
        out.offset.x += step.x;
        out.offset.z += step.z;
        if (options.observer != nullptr) {
            // resolve() clears the stop it did not use; a pushed-out mover never looks at it.
            const bool stopped = s.stop.kind != 0 && s.walls.blocked.count == 0 && s.walls.bodies.count == 0;
            if (stopped) {
                tell_stop(s);
            }
            if (options.observer->round != nullptr) {
                options.observer->round(options.observer->user, s.second_round ? 1 : 0, m.from_x, m.from_z, step, rest,
                                        stopped ? s.stop.normal_x : 0, stopped ? s.stop.normal_z : 0);
            }
        }
        if (!s.second_round) {
            out.contact_distance = s.contact.distance;
            out.radius = m.radius;
            out.blocked = s.walls.blocked.count != 0;
            out.blocked_min = s.walls.blocked.from;
            out.blocked_max = s.walls.blocked.to;
            out.contact_index = s.contact.index;
        }
        if (s.second_round) {
            break;
        }
        if (s.walls.blocked.count != 0 || s.walls.bodies.count != 0) {
            const int count = s.walls.near.count < 9 ? s.walls.near.count : 8;
            for (int i = 0; i < count; ++i) {
                std::uint8_t* mark = s.walls.near_marks[static_cast<std::size_t>(i)];
                *mark = static_cast<std::uint8_t>(*mark + 1);
            }
        } else if (s.stop.kind != 0) {
            *s.stop.mark = static_cast<std::uint8_t>(*s.stop.mark + 1);
        } else {
            break;
        }
        s.second_round = true;
        start_second_round(s, step, rest);
    }

    if (s.height_disabled == 1) {
        out.ground_height = kNoGround << 4;
        out.ground_type = 0;
    } else {
        out.ground_type = s.ground_type;
        out.ground_height = ground_at(s, m.from_x + (step.x >> 4), m.from_z + (step.z >> 4)) << 4;
    }
    out.slope_x = static_cast<std::int16_t>(s.slope_x);
    out.slope_z = static_cast<std::int16_t>(s.slope_z);
    out.code = corner ? -1 : (s.second_round ? 1 : 0);
    return out;
}

}  // namespace sh::world
