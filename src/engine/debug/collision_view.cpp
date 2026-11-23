// SPDX-License-Identifier: GPL-3.0-or-later
//
// The collision view (collision_view.h).

#include "engine/debug/collision_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "engine/camera/camera_host.h"
#include "engine/game_state/game_state.h"
#include "engine/math/geometry_state.h"
#include "render/draw3d/draw3d.h"
#include "render/game_view.h"
#include "render/gpu/render_state.h"

namespace sh::debug {

namespace {

namespace wg = sh::world::game;
namespace d3 = sh::draw3d;
using sh::world::ActiveCell;
using sh::world::CeilingBox;
using sh::world::CellCollision;
using sh::world::EdgeTop;
using sh::world::FloorPlane;
using sh::world::GroundSample;
using sh::world::MoveObstacle;
using sh::world::MoveResult;
using sh::world::MoveTouch;
using sh::world::Pillar;
using sh::world::Vec3;
using sh::world::WallEdge;

constexpr std::uint8_t kNoFloor = 255;
constexpr double kQ8 = 256.0;    // collision data units per metre
constexpr double kQ12 = 4096.0;  // world units per metre

// How far around the player the cells' edges and pillars are drawn, and the half-size and step of the ground
// lattice (metres).
constexpr double kEdgeRange = 14.0;
constexpr int kLatticeHalf = 10;
constexpr double kLatticeStep = 0.5;
// A wall has no bottom in the data (it blocks everything below its top), so it is drawn down to the ground beside
// it and no taller than this.
constexpr double kWallDrawn = 2.2;
// The per-frame move is a few centimetres: its arrows are drawn this many times longer.
constexpr double kMoveScale = 12.0;

struct P3 {
    double x = 0, y = 0, z = 0;  // metres, world axes (+Y down)
};

P3 operator+(const P3& a, const P3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
P3 operator*(const P3& a, double k) { return {a.x * k, a.y * k, a.z * k}; }

struct Color {
    std::uint8_t r = 0, g = 0, b = 0;
};

constexpr Color kWall{90, 255, 60};         // a wall face that reaches the player's feet: it blocks
constexpr Color kWallLow{30, 110, 40};      // a wall face whose top is under the feet: stepped over
constexpr Color kFloorEdge{70, 110, 200};   // an edge with a floor on both sides: never a wall
constexpr Color kSwitchedOff{80, 80, 80};   // its collision group is off
constexpr Color kPushed{255, 30, 30};       // touching this frame / stopped the sweep
constexpr Color kNear{255, 150, 0};         // within reach this frame
constexpr Color kPillar{0, 220, 255};
constexpr Color kBody{255, 60, 255};
constexpr Color kPlayer{255, 255, 255};
constexpr Color kPlayerWall{255, 150, 0};   // the ground classification called the place a wall
constexpr Color kCeiling{170, 90, 255};
constexpr Color kAsked{255, 255, 255};
constexpr Color kAllowed{60, 255, 60};
constexpr Color kSlide{255, 230, 0};
constexpr Color kNormal{255, 230, 0};
constexpr Color kHole{200, 0, 0};

// Ground types are footstep materials: one hue each, kept dim so the lattice stays behind the walls.
Color ground_color(int type) {
    static constexpr std::array<Color, 16> kTypes{{
        {110, 110, 110}, {150, 110, 60}, {60, 140, 60},  {150, 150, 60}, {60, 140, 150}, {150, 80, 130},
        {90, 90, 170},   {170, 100, 40}, {40, 150, 110}, {130, 60, 60},  {110, 150, 40}, {60, 100, 150},
        {150, 40, 40},   {130, 130, 90}, {90, 130, 130}, {130, 90, 130},
    }};
    return kTypes[static_cast<std::size_t>(type) & 15];
}

Color touch_color(MoveTouch touch) {
    return touch == MoveTouch::Near ? kNear : kPushed;
}

bool has_ground(const GroundSample& s) {
    const GroundSample none = sh::world::no_ground();
    return !(s.height == none.height && s.ground_type == none.ground_type);
}

// Lines of the view as thin quads of the frame's world layer. Each quad carries one depth, so a line that runs away
// from the eye is cut into pieces short in depth.
class Painter {
public:
    bool begin() {
        const sh::camera::Matrix& ws = sh::camera::camera_host().world_screen();
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                rot_[r][c] = ws.m[r][c] / kQ12;
            }
            trans_[r] = ws.t[r];
        }
        const sh::math::GeometryState& g = sh::math::geometry();
        h_ = g.screen_distance();
        ofx_ = g.screen_offset_x();
        ofy_ = g.screen_offset_y();
        // Without sub-pixel vertices a quad thinner than a pixel of the 320x240 grid can vanish.
        half_ = sh::render::precise().enabled != 0 ? 0.4 : 0.75;
        return h_ > 0;
    }

    void line(const P3& a, const P3& b, const Color& color, double width = 1.0) {
        View va = view(a);
        View vb = view(b);
        if ((va.z < kNear && vb.z < kNear) || (va.z > kFar && vb.z > kFar)) {
            return;
        }
        if (va.z < kNear) {
            va = mix(va, vb, (kNear - va.z) / (vb.z - va.z));
        } else if (vb.z < kNear) {
            vb = mix(vb, va, (kNear - vb.z) / (va.z - vb.z));
        }
        const int pieces = std::clamp(static_cast<int>(std::ceil(std::abs(vb.z - va.z) / kPieceDepth)), 1, 48);
        View from = va;
        for (int i = 1; i <= pieces; ++i) {
            const View to = mix(va, vb, static_cast<double>(i) / pieces);
            piece(from, to, color, width);
            from = to;
        }
    }

    // A horizontal circle.
    void ring(const P3& centre, double radius, const Color& color, double width = 1.0) {
        constexpr int kSides = 16;
        P3 previous = rim(centre, radius, 0);
        for (int i = 1; i <= kSides; ++i) {
            const P3 next = rim(centre, radius, i * (2.0 * 3.14159265358979323846 / kSides));
            line(previous, next, color, width);
            previous = next;
        }
    }

    // A vertical cylinder between two heights.
    void cylinder(double x, double z, double top, double bottom, double radius, const Color& color,
                  double width = 1.0) {
        ring({x, top, z}, radius, color, width);
        ring({x, bottom, z}, radius, color, width);
        for (int i = 0; i < 4; ++i) {
            const P3 at = rim({x, top, z}, radius, i * (3.14159265358979323846 / 2.0));
            line(at, {at.x, bottom, at.z}, color, width);
        }
    }

    // An arrow from `from` along `by`.
    void arrow(const P3& from, const P3& by, const Color& color) {
        const P3 tip = from + by;
        line(from, tip, color, 1.6);
        const double length = std::sqrt(by.x * by.x + by.z * by.z);
        if (length < 1e-6) {
            return;
        }
        const double ux = by.x / length, uz = by.z / length;
        const double head = std::min(0.08, length * 0.4);
        line(tip, {tip.x - (ux + uz) * head, tip.y, tip.z - (uz - ux) * head}, color, 1.6);
        line(tip, {tip.x - (ux - uz) * head, tip.y, tip.z - (uz + ux) * head}, color, 1.6);
    }

private:
    struct View {
        double x = 0, y = 0, z = 0;  // view space, 1/256 m
    };

    static constexpr double kNear = 40.0;
    static constexpr double kFar = 60.0 * kQ8;
    static constexpr double kPieceDepth = 40.0;
    // Lines lie in the planes of the visible walls and floors: drawn this much nearer, they win the depth test
    // against the surface they mark and lose it against anything standing in front.
    static constexpr double kDepthBias = 12.0;

    static P3 rim(const P3& centre, double radius, double angle) {
        return {centre.x + std::cos(angle) * radius, centre.y, centre.z + std::sin(angle) * radius};
    }

    static View mix(const View& a, const View& b, double t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    }

    View view(const P3& p) const {
        const double w[3] = {p.x * kQ8, p.y * kQ8, p.z * kQ8};
        View v;
        v.x = rot_[0][0] * w[0] + rot_[0][1] * w[1] + rot_[0][2] * w[2] + trans_[0];
        v.y = rot_[1][0] * w[0] + rot_[1][1] * w[1] + rot_[1][2] * w[2] + trans_[1];
        v.z = rot_[2][0] * w[0] + rot_[2][1] * w[1] + rot_[2][2] * w[2] + trans_[2];
        return v;
    }

    void piece(const View& a, const View& b, const Color& color, double width) {
        const double ax = ofx_ + a.x * h_ / a.z, ay = ofy_ + a.y * h_ / a.z;
        const double bx = ofx_ + b.x * h_ / b.z, by = ofy_ + b.y * h_ / b.z;
        // Screen positions are 16-bit; a piece that far outside the picture shows nothing anyway.
        constexpr double kLimit = 12000.0;
        if (std::abs(ax) > kLimit || std::abs(ay) > kLimit || std::abs(bx) > kLimit || std::abs(by) > kLimit) {
            return;
        }
        double nx = -(by - ay), ny = bx - ax;
        const double length = std::sqrt(nx * nx + ny * ny);
        if (length < 1e-4) {
            nx = 1.0;
            ny = 0.0;
        } else {
            nx /= length;
            ny /= length;
        }
        const double half = half_ * width;
        const double depth = std::max(1.0, (a.z + b.z) * 0.5 - kDepthBias);
        const auto sz = static_cast<std::uint32_t>(depth);

        d3::Polygon quad;
        quad.count = 4;
        quad.no_flashlight = true;
        const double corners[4][3] = {{ax + nx * half, ay + ny * half, a.z},
                                      {ax - nx * half, ay - ny * half, a.z},
                                      {bx + nx * half, by + ny * half, b.z},
                                      {bx - nx * half, by - ny * half, b.z}};
        for (int k = 0; k < 4; ++k) {
            d3::Vertex& v = quad.v[static_cast<std::size_t>(k)];
            v.xy = {static_cast<std::int16_t>(std::lround(corners[k][0])),
                    static_cast<std::int16_t>(std::lround(corners[k][1]))};
            v.precise = d3::Precise{static_cast<float>(corners[k][0]), static_cast<float>(corners[k][1]),
                                    static_cast<float>(corners[k][2])};
            v.color = {color.r, color.g, color.b};
        }
        d3::Depth d;
        d.sz = {sz, sz, sz, sz};
        d.source = d3::DepthSource::Exact;
        quad.depth = d;
        // The slot is the characters' scale (an eighth of the view depth); a zero depth maximum leaves the frame's
        // own untouched.
        d3::submit(quad, sh::render::world_order_key(static_cast<int>(sz >> 3) - 1), 0);
    }

    double rot_[3][3] = {};
    double trans_[3] = {};
    double h_ = 0, ofx_ = 0, ofy_ = 0;
    double half_ = 0.4;
};

// A point of a placed cell (cell space Q.8) in world metres.
P3 cell_point(const ActiveCell& ac, double x, double y, double z) {
    return {(ac.placed.origin_x + x) / kQ8, y / kQ8, (ac.placed.origin_z + z) / kQ8};
}

double distance_to_segment(double px, double pz, const P3& a, const P3& b) {
    const double dx = b.x - a.x, dz = b.z - a.z;
    const double square = dx * dx + dz * dz;
    double t = square > 0 ? ((px - a.x) * dx + (pz - a.z) * dz) / square : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return std::hypot(px - (a.x + dx * t), pz - (a.z + dz * t));
}

// The ground the game would find at a point (metres), or `fallback` where it finds none.
double ground_or(double x, double z, double fallback) {
    const GroundSample s = wg::peek_ground(static_cast<std::int32_t>(std::lround(x * kQ12)),
                                           static_cast<std::int32_t>(std::lround(z * kQ12)));
    return has_ground(s) ? s.height / kQ12 : fallback;
}

}  // namespace

CollisionView& collision_view() {
    static CollisionView view;
    return view;
}

void CollisionView::set_enabled(bool on) {
    if (on == enabled_) {
        return;
    }
    enabled_ = on;
    if (on) {
        tap_.observer.user = this;
        tap_.observer.wall = on_wall;
        tap_.observer.pillar = on_pillar;
        tap_.observer.body = on_body;
        tap_.observer.round = on_round;
        tap_.begin = on_begin;
        tap_.end = on_end;
        tap_.classified = on_classified;
        wg::set_player_move_tap(&tap_);
    } else {
        wg::set_player_move_tap(nullptr);
        pass_ = Pass{};
        cells_.clear();
        cells_.shrink_to_fit();
    }
}

void CollisionView::on_begin(void* user, const wg::MovePassView& pass) {
    auto& self = *static_cast<CollisionView*>(user);
    Pass& p = self.pass_;
    if (self.drawn_ || !p.seen) {
        p.touched.clear();
        self.drawn_ = false;
    }
    p.seen = true;
    p.cylinder = pass.cylinder;
    p.asked = pass.asked;
    p.groups = pass.groups;
    p.walls_to_ceiling = pass.walls_to_ceiling;
    p.rounds[0] = Round{};
    p.rounds[1] = Round{};
    p.wall = p.ledge = p.no_ground = false;
    p.bodies.clear();
    for (const sh::world::MoveBody& body : pass.bodies) {
        p.bodies.push_back(Body{&body.main, body.main});
        for (const MoveObstacle& part : body.parts) {
            p.bodies.push_back(Body{&part, part});
        }
    }
    p.ceilings.assign(pass.ceilings.begin(), pass.ceilings.end());
}

void CollisionView::on_end(void* user, const MoveResult& result) {
    static_cast<CollisionView*>(user)->pass_.result = result;
}

void CollisionView::on_classified(void* user, bool wall, bool ledge, bool no_ground) {
    Pass& p = static_cast<CollisionView*>(user)->pass_;
    p.wall = wall;
    p.ledge = ledge;
    p.no_ground = no_ground;
}

void CollisionView::touch(const ActiveCell& ac, bool pillar, std::size_t index, MoveTouch touch, const EdgeTop& top) {
    const CellCollision* cell = ac.placed.cell;
    for (Touched& t : pass_.touched) {
        if (t.cell == cell && t.pillar == pillar && t.index == index) {
            // Touching outranks being near.
            if (touch != MoveTouch::Near) {
                t.touch = touch;
            }
            return;
        }
    }
    Touched t{cell, pillar, index, touch};
    std::snprintf(t.name, sizeof(t.name), "%s", cell->name.c_str());
    t.origin_x = ac.placed.origin_x;
    t.origin_z = ac.placed.origin_z;
    t.top = top;
    pass_.touched.push_back(t);
}

const CollisionView::Touched* CollisionView::touched(const CellCollision* cell, bool pillar, std::size_t index) const {
    for (const Touched& t : pass_.touched) {
        if (t.cell == cell && t.pillar == pillar && t.index == index) {
            return &t;
        }
    }
    return nullptr;
}

void CollisionView::on_wall(void* user, const ActiveCell& cell, std::uint8_t edge, const EdgeTop& top,
                            MoveTouch touch) {
    static_cast<CollisionView*>(user)->touch(cell, false, edge, touch, top);
}

void CollisionView::on_pillar(void* user, const ActiveCell& cell, std::size_t pillar, MoveTouch touch) {
    static_cast<CollisionView*>(user)->touch(cell, true, pillar, touch, EdgeTop{});
}

void CollisionView::on_body(void* user, const MoveObstacle& body, MoveTouch touch) {
    for (Body& b : static_cast<CollisionView*>(user)->pass_.bodies) {
        if (b.source == &body) {
            b.touched = true;
            b.touch = touch;
        }
    }
}

void CollisionView::on_round(void* user, int round, std::int32_t from_x, std::int32_t from_z, Vec3 step, Vec3 rest,
                             std::int32_t normal_x, std::int32_t normal_z) {
    Round& r = static_cast<CollisionView*>(user)->pass_.rounds[round & 1];
    r.ran = true;
    r.from_x = from_x;
    r.from_z = from_z;
    r.step = step;
    r.rest = rest;
    r.normal_x = normal_x;
    r.normal_z = normal_z;
}

void CollisionView::draw() {
    if (!enabled_) {
        return;
    }
    Painter paint;
    if (!paint.begin()) {
        return;
    }
    drawn_ = true;

    const Pass& p = pass_;
    const sh::entities::Vec3 player = sh::game_state::chara_position(0);
    const double px = player.x / kQ12, pz = player.z / kQ12;
    // The player's feet as the pass measures them (Q.8), and the ground the walls are drawn down to where the game
    // finds none beside them.
    const std::int32_t feet_q12 = p.seen ? p.cylinder.position.y + p.cylinder.bottom : player.y;
    const std::int32_t feet_q8 = feet_q12 >> 4;
    const double feet = feet_q12 / kQ12;
    const bool walls_to_ceiling = p.seen ? p.walls_to_ceiling : true;

    const std::uint16_t groups = wg::collision_scene(cells_);
    const auto group_on = [groups](int bit) { return ((groups >> bit) & 1) != 0; };

    // --- The ground the queries answer, on a lattice fixed to the world ---
    {
        constexpr int kSide = kLatticeHalf * 2 + 1;
        static std::array<GroundSample, kSide * kSide> samples;
        const double base_x = std::floor(px / kLatticeStep) * kLatticeStep - kLatticeHalf * kLatticeStep;
        const double base_z = std::floor(pz / kLatticeStep) * kLatticeStep - kLatticeHalf * kLatticeStep;
        for (int row = 0; row < kSide; ++row) {
            for (int column = 0; column < kSide; ++column) {
                samples[static_cast<std::size_t>(row * kSide + column)] =
                    wg::peek_ground(static_cast<std::int32_t>(std::lround((base_x + column * kLatticeStep) * kQ12)),
                                    static_cast<std::int32_t>(std::lround((base_z + row * kLatticeStep) * kQ12)));
            }
        }
        const auto node = [&](int row, int column) {
            const GroundSample& s = samples[static_cast<std::size_t>(row * kSide + column)];
            return P3{base_x + column * kLatticeStep, s.height / kQ12, base_z + row * kLatticeStep};
        };
        for (int row = 0; row < kSide; ++row) {
            for (int column = 0; column < kSide; ++column) {
                const GroundSample& s = samples[static_cast<std::size_t>(row * kSide + column)];
                const P3 at = node(row, column);
                if (!has_ground(s)) {
                    // A hole: marked at the player's feet, where it matters.
                    if (std::hypot(at.x - px, at.z - pz) < 3.0) {
                        const double k = 0.06;
                        paint.line({at.x - k, feet, at.z - k}, {at.x + k, feet, at.z + k}, kHole);
                        paint.line({at.x - k, feet, at.z + k}, {at.x + k, feet, at.z - k}, kHole);
                    }
                    continue;
                }
                const Color color = ground_color(s.ground_type);
                // Neighbours on one surface are joined; a step between two floors leaves a gap.
                const auto join = [&](int r2, int c2) {
                    const GroundSample& o = samples[static_cast<std::size_t>(r2 * kSide + c2)];
                    if (has_ground(o) && std::abs(o.height - s.height) < static_cast<std::int32_t>(0.45 * kQ12)) {
                        paint.line(at, node(r2, c2), color, 0.7);
                    }
                };
                if (column + 1 < kSide) {
                    join(row, column + 1);
                }
                if (row + 1 < kSide) {
                    join(row + 1, column);
                }
            }
        }
    }

    // --- Wall edges and pillars of the cells the passes walk ---
    for (const ActiveCell& ac : cells_) {
        const CellCollision* cell = ac.placed.cell;
        if (cell == nullptr) {
            continue;
        }
        for (std::size_t i = 0; i < cell->edges.size(); ++i) {
            const WallEdge& edge = cell->edges[i];
            const EdgeTop top = sh::world::edge_top(*cell, edge, walls_to_ceiling);
            P3 a = cell_point(ac, top.start.x, top.start.y, top.start.z);
            P3 b = cell_point(ac, top.end.x, top.end.y, top.end.z);
            if (distance_to_segment(px, pz, a, b) > kEdgeRange) {
                continue;
            }
            const bool on = group_on(edge.group_high * 4 | edge.group_low);
            const bool open_left = edge.floor_left == kNoFloor;
            const bool open_right = edge.floor_right == kNoFloor;
            const Touched* hit = touched(cell, false, i);
            if (!open_left && !open_right) {
                paint.line(a, b, on ? kFloorEdge : kSwitchedOff, 0.8);
                continue;
            }
            Color color = kSwitchedOff;
            double width = 1.0;
            if (on) {
                color = sh::world::wall_reaches(top, feet_q8) ? kWall : kWallLow;
                if (hit != nullptr) {
                    color = touch_color(hit->touch);
                    width = 2.2;
                }
            }
            // The side without a floor is the one the face looks at; (dir_z, -dir_x) points to the right side.
            const double nx = edge.dir_z / kQ12, nz = -edge.dir_x / kQ12;
            const double side = open_right && open_left ? ((px - a.x) * nx + (pz - a.z) * nz >= 0 ? 1.0 : -1.0)
                                                        : (open_right ? 1.0 : -1.0);
            const double off_x = nx * side * 0.15, off_z = nz * side * 0.15;
            double floor_a = ground_or(a.x + off_x, a.z + off_z, feet);
            double floor_b = ground_or(b.x + off_x, b.z + off_z, feet);
            {
                // An end can sit under another floor (an awning, a roof); the wall then stands on what its middle
                // and other end agree on.
                const double floor_m = ground_or((a.x + b.x) * 0.5 + off_x, (a.z + b.z) * 0.5 + off_z, feet);
                const double median = std::max(std::min(floor_a, floor_b), std::min(std::max(floor_a, floor_b), floor_m));
                if (std::abs(floor_a - median) > 0.6) {
                    floor_a = median;
                }
                if (std::abs(floor_b - median) > 0.6) {
                    floor_b = median;
                }
            }
            const double top_a = std::max(a.y, floor_a - kWallDrawn);
            const double top_b = std::max(b.y, floor_b - kWallDrawn);
            a.y = top_a;
            b.y = top_b;
            paint.line(a, b, color, width);
            if (top_a < floor_a || top_b < floor_b) {
                const P3 foot_a{a.x, std::max(floor_a, top_a), a.z};
                const P3 foot_b{b.x, std::max(floor_b, top_b), b.z};
                paint.line(foot_a, foot_b, color, width);
                paint.line(a, foot_a, color, width);
                paint.line(b, foot_b, color, width);
                // Which way the face looks: a tick out of the middle of its foot (two for a wall open on both sides).
                const P3 middle = (foot_a + foot_b) * 0.5;
                paint.line(middle, {middle.x + nx * side * 0.2, middle.y, middle.z + nz * side * 0.2}, color, width);
                if (open_left && open_right) {
                    paint.line(middle, {middle.x - nx * side * 0.2, middle.y, middle.z - nz * side * 0.2}, color,
                               width);
                }
            }
        }
        for (std::size_t i = 0; i < cell->pillars.size(); ++i) {
            const Pillar& pillar = cell->pillars[i];
            const P3 centre = cell_point(ac, pillar.position.x, pillar.position.y, pillar.position.z);
            const double radius = pillar.radius / kQ8;
            if (radius <= 0 || std::hypot(centre.x - px, centre.z - pz) - radius > kEdgeRange) {
                continue;
            }
            const Touched* hit = touched(cell, true, i);
            Color color = group_on(pillar.enable_group) ? kPillar : kSwitchedOff;
            if (hit != nullptr) {
                color = touch_color(hit->touch);
            }
            const double floor = ground_or(centre.x + radius + 0.1, centre.z, feet);
            const double top = std::max(centre.y, floor - kWallDrawn);
            paint.cylinder(centre.x, centre.z, top, std::max(floor, top), radius, color, hit != nullptr ? 2.2 : 1.0);
        }
    }

    if (!p.seen) {
        return;
    }

    // --- The characters in the player's way, the low ceilings over him, and the player ---
    for (const Body& body : p.bodies) {
        paint.cylinder(body.shape.x / kQ8, body.shape.z / kQ8, body.shape.low / kQ8, body.shape.high / kQ8,
                       body.shape.other_radius / kQ8, body.touched ? touch_color(body.touch) : kBody,
                       body.touched ? 2.2 : 1.0);
    }
    for (const CeilingBox& box : p.ceilings) {
        const double y = box.height / kQ12;
        const P3 c0{box.min_x / kQ12, y, box.min_z / kQ12}, c1{box.max_x / kQ12, y, box.min_z / kQ12};
        const P3 c2{box.max_x / kQ12, y, box.max_z / kQ12}, c3{box.min_x / kQ12, y, box.max_z / kQ12};
        paint.line(c0, c1, kCeiling);
        paint.line(c1, c2, kCeiling);
        paint.line(c2, c3, kCeiling);
        paint.line(c3, c0, kCeiling);
        paint.line(c0, c2, kCeiling, 0.7);
        paint.line(c1, c3, kCeiling, 0.7);
    }
    const P3 body{p.cylinder.position.x / kQ12, p.cylinder.position.y / kQ12, p.cylinder.position.z / kQ12};
    paint.cylinder(body.x, body.z, body.y + p.cylinder.top / kQ12, feet, p.cylinder.radius / kQ12,
                   p.wall ? kPlayerWall : kPlayer);

    // --- The move: asked, allowed, and the slide along what stopped the first round ---
    const P3 start{body.x, feet - 0.05, body.z};
    const auto flat = [](const Vec3& v) { return P3{v.x / kQ12, 0.0, v.z / kQ12}; };
    paint.arrow(start, flat(p.asked) * kMoveScale, kAsked);
    paint.arrow({start.x, start.y - 0.02, start.z}, flat(p.result.offset) * kMoveScale, kAllowed);
    const Round& first = p.rounds[0];
    if (first.ran && (first.normal_x != 0 || first.normal_z != 0)) {
        const P3 contact = start + flat(first.step) * kMoveScale;
        const double length = std::hypot(static_cast<double>(first.normal_x), static_cast<double>(first.normal_z));
        paint.arrow(contact, P3{first.normal_x / length, 0.0, first.normal_z / length} * 0.3, kPushed);
        paint.arrow(contact, flat(first.rest) * kMoveScale, kSlide);
    }

    // --- The ground the pass answered: its normal at the place the player ends up ---
    if (p.result.ground_type != 0 || p.result.ground_height != sh::world::no_ground().height) {
        const P3 foot{body.x + p.result.offset.x / kQ12, p.result.ground_height / kQ12,
                      body.z + p.result.offset.z / kQ12};
        // The height grows by slope_x per unit of x (and +Y is down): the upward normal is (slope_x, -1, slope_z).
        const double sx = p.result.slope_x / kQ12, sz = p.result.slope_z / kQ12;
        const double length = std::sqrt(sx * sx + 1.0 + sz * sz);
        paint.line(foot, foot + P3{sx / length, -1.0 / length, sz / length} * 0.6, kNormal, 1.6);
        paint.ring(foot, 0.08, ground_color(p.result.ground_type), 1.6);
    }
}

std::vector<std::string> CollisionView::describe() const {
    std::vector<std::string> lines;
    char text[160];
    const auto add = [&](const char* format, auto... values) {
        if constexpr (sizeof...(values) == 0)
            std::snprintf(text, sizeof(text), "%s", format);
        else
            std::snprintf(text, sizeof(text), format, values...);
        lines.emplace_back(text);
    };
    if (!enabled_) {
        add("collision view is off (collvis 1)");
        return lines;
    }
    const Pass& p = pass_;
    if (!p.seen) {
        add("no movement pass of the player seen yet");
        return lines;
    }
    add("cylinder %d,%d,%d bottom %d top %d radius %d (Q12)", p.cylinder.position.x, p.cylinder.position.y,
        p.cylinder.position.z, p.cylinder.bottom, p.cylinder.top, p.cylinder.radius);
    add("asked %d,%d allowed %d,%d code %d%s", p.asked.x, p.asked.z, p.result.offset.x, p.result.offset.z,
        p.result.code, p.result.slope_capped ? " (slope fix)" : "");
    add("ground %d type %d slope %d,%d ceiling %d%s%s%s", p.result.ground_height, p.result.ground_type,
        p.result.slope_x, p.result.slope_z, p.result.ceiling_height, p.wall ? " WALL" : "", p.ledge ? " ledge" : "",
        p.no_ground ? " no-ground" : "");
    int pushed = 0, close = 0, stopped = 0;
    for (const Touched& t : p.touched) {
        pushed += t.touch == MoveTouch::Pushed;
        close += t.touch == MoveTouch::Near;
        stopped += t.touch == MoveTouch::Stopped;
    }
    add("touched: %d pushed, %d near, %d stopped; %zu bodies, %zu ceilings, %zu cells, groups %04X", pushed, close,
        stopped, p.bodies.size(), p.ceilings.size(), cells_.size(), static_cast<unsigned>(p.groups));
    static constexpr const char* kTouch[] = {"pushed", "near", "stopped"};
    for (const Touched& t : p.touched) {
        const char* what = kTouch[static_cast<int>(t.touch)];
        if (t.pillar) {
            add("  %s pillar %zu: %s", t.name, t.index, what);
        } else {
            add("  %s edge %zu: %s, top %d,%d,%d - %d,%d,%d (Q8)", t.name, t.index, what,
                t.origin_x + t.top.start.x, t.top.start.y, t.origin_z + t.top.start.z, t.origin_x + t.top.end.x,
                t.top.end.y, t.origin_z + t.top.end.z);
        }
    }
    return lines;
}

}  // namespace sh::debug

extern "C" void Pc_CollisionViewDraw(void) {
    sh::debug::collision_view().draw();
}
