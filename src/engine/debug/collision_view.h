// SPDX-License-Identifier: GPL-3.0-or-later
//
// The collision view: draws, in the 3D world and tested against the scene's depth,
// the collision the movement queries walk around the player and what the player's last movement pass did with it.
//
// Nothing here is a second model of the collision. The geometry is read from the placed cells the passes walk
// (sh::world::game::collision_scene), wall tops come from the pass's own rule (sh::world::edge_top, wall_reaches),
// the ground lattice is the ground query itself on a copy of its ledger (peek_ground), and what was touched is told
// by the pass through its read-only observer (sh::world::game::MoveTap). Off, it costs one pointer test per pass
// and one flag test per frame.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/world/game/character_world.h"

namespace sh::debug {

class CollisionView {
public:
    bool enabled() const { return enabled_; }
    void set_enabled(bool on);

    // Queues the view into the frame's world layer. Called in the world's draw step; does nothing when off.
    void draw();

    // The player's last watched pass as text lines (console `collinfo`).
    std::vector<std::string> describe() const;

private:
    // An edge or pillar a pass touched. The cell is an identity only (it may be gone by the time of the draw).
    struct Touched {
        const world::CellCollision* cell = nullptr;
        bool pillar = false;
        std::size_t index = 0;
        world::MoveTouch touch = world::MoveTouch::Near;
        // For the text report: the cell's name and origin (Q.8) and, of an edge, the top the pass tested.
        char name[12] = {};
        std::int32_t origin_x = 0, origin_z = 0;
        world::EdgeTop top;
    };
    struct Body {
        const world::MoveObstacle* source = nullptr;  // identity inside the pass
        world::MoveObstacle shape;
        bool touched = false;
        world::MoveTouch touch = world::MoveTouch::Near;
    };
    struct Round {
        bool ran = false;
        std::int32_t from_x = 0, from_z = 0;  // Q.8 world
        world::Vec3 step, rest;               // Q.12
        std::int32_t normal_x = 0, normal_z = 0;
    };
    struct Pass {
        bool seen = false;
        world::MoveCylinder cylinder;
        world::Vec3 asked;
        std::uint16_t groups = 0;
        bool walls_to_ceiling = false;
        std::vector<Body> bodies;
        std::vector<world::CeilingBox> ceilings;
        std::vector<Touched> touched;
        Round rounds[2];
        world::MoveResult result;
        bool wall = false, ledge = false, no_ground = false;
    };

    static void on_begin(void* user, const world::game::MovePassView& pass);
    static void on_end(void* user, const world::MoveResult& result);
    static void on_classified(void* user, bool wall, bool ledge, bool no_ground);
    static void on_wall(void* user, const world::ActiveCell& cell, std::uint8_t edge, const world::EdgeTop& top,
                        world::MoveTouch touch);
    static void on_pillar(void* user, const world::ActiveCell& cell, std::size_t pillar, world::MoveTouch touch);
    static void on_body(void* user, const world::MoveObstacle& body, world::MoveTouch touch);
    static void on_round(void* user, int round, std::int32_t from_x, std::int32_t from_z, world::Vec3 step,
                         world::Vec3 rest, std::int32_t normal_x, std::int32_t normal_z);

    void touch(const world::ActiveCell& cell, bool pillar, std::size_t index, world::MoveTouch touch,
               const world::EdgeTop& top);
    const Touched* touched(const world::CellCollision* cell, bool pillar, std::size_t index) const;

    bool enabled_ = false;
    bool drawn_ = false;  // a frame was drawn since the last pass: the next pass starts a fresh set of touches
    world::game::MoveTap tap_;
    Pass pass_;
    std::vector<world::ActiveCell> cells_;
};

CollisionView& collision_view();

}  // namespace sh::debug
