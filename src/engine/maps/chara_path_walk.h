/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

/* A story character walking through a list of waypoints in a cutscene (header slot charaPathWaypointExecute of
 * a map with charas.json). One call advances the walk by one phase: face the
 * first point, walk from point to point while turning toward the one after, turn to the final heading, report
 * the arrival. The character keeps its own phase and animation request; the next waypoint, the heading being
 * turned to and the last distance are kept once for every character, so two characters walking at the same time
 * share them, as they did in the original. The executable owns one walker and resets it on every map load (the
 * original's words started at zero with each overlay). */

#include <cstdint>

#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"

namespace sh::maps {

struct WalkPoint {
    std::int32_t x = 0;  // Q12 metres
    std::int32_t z = 0;
};

/* The words of the walking character: copied in before a step and written back after it. */
struct PathWalkActor {
    std::int32_t x = 0;              // position, Q12
    std::int32_t z = 0;
    std::int16_t yaw = 0;            // heading, Q12 angle (16 bits, as the character stores it)
    std::int32_t phase = 0;          // the walk phase (0 = not started)
    std::int32_t anim = 0;           // the animation the character is asked to play
    std::int32_t anim_request = 0;   // the request the character's update compares against
    std::int32_t arrived = 0;        // becomes 1 when the walk ends
    bool restart_state = false;      // the character's state step must go back to 0
    bool stop_motion = false;        // the character's move distance and speed must be cleared
};

class PathWalk {
public:
    /* Animations a turn on the spot asks for. */
    static constexpr std::int32_t kTurnRightAnim = 3;
    static constexpr std::int32_t kTurnLeftAnim = 4;
    /* A heading this close (360 / 64 degrees) counts as reached. */
    static constexpr std::int32_t kFacingTolerance = 64;
    /* Under 0.15 m (Q8) from the first point the walk skips straight to the final turn. */
    static constexpr std::int32_t kAlreadyThere = 38;
    /* 0.15 m in Q12: each walk animation number adds this much to the distance a waypoint counts as reached. */
    static constexpr std::int32_t kReachPerAnim = 614;
    /* Turn rates per 30 fps frame while walking: slow under 45 degrees, fast otherwise (Q12 angles). */
    static constexpr std::int32_t kSlowTurn = 32;
    static constexpr std::int32_t kFastTurn = 256;
    static constexpr std::int32_t kSlowTurnLimit = 512;
    /* One 30 fps frame in Q12 seconds. */
    static constexpr std::int32_t kFrame30 = 136;

    void reset() { *this = PathWalk{}; }

    std::int16_t next_point() const { return next_; }
    std::int16_t heading() const { return heading_; }
    std::int32_t last_distance() const { return last_distance_; }
    void set_state(std::int16_t next, std::int16_t heading, std::int32_t last_distance) {
        next_ = next;
        heading_ = heading;
        last_distance_ = last_distance;
    }

    /* One phase of the walk. `point_at(i)` returns waypoint i; like the original it may be asked for the
     * point just past the last one. `walk_anim` is the walking animation, `final_heading` the heading to end
     * with (any Q12 angle), `count` the number of waypoints, `delta_time` the frame time in Q12 seconds. True
     * when the walk has ended on this call. */
    template <class PointAt>
    bool step(PathWalkActor& a, std::int32_t walk_anim, PointAt&& point_at, std::int32_t final_heading,
              std::int32_t count, std::int32_t delta_time) {
        const std::int32_t end_heading = unsigned_angle(final_heading);
        switch (a.phase) {
            case 0:
                next_ = 0;
                a.phase = 1;
                break;

            case 1: {
                const WalkPoint first = point_at(0);
                if (first.x == a.x && first.z == a.z) {
                    a.phase = 6;
                    break;
                }
                heading_ = static_cast<std::int16_t>(unsigned_angle(heading_to(a, first)));
                const std::int16_t turn = shortest_turn(a.yaw, heading_);
                if (facing(turn)) {
                    a.yaw = heading_;
                    a.phase = 3;
                } else {
                    turn_on_spot(a, turn);
                    a.phase = 2;
                }
                break;
            }

            case 2:
                if (facing(shortest_turn(a.yaw, heading_))) {
                    a.yaw = heading_;
                    a.phase = 3;
                }
                break;

            case 3: {
                const std::int32_t distance = distance_to(a, point_at(0));
                if ((distance < 0 ? -distance : distance) < kAlreadyThere) {
                    a.anim = 0;
                    a.restart_state = true;
                    a.phase = 8;
                } else {
                    a.anim = walk_anim;
                    a.anim_request = walk_anim;
                    a.restart_state = true;
                    a.phase = 4;
                }
                break;
            }

            case 4: {
                const WalkPoint target = point_at(next_);
                heading_ = static_cast<std::int16_t>(unsigned_angle(heading_to(a, target)));
                last_distance_ = distance_to(a, target);
                a.phase = 5;
                break;
            }

            case 5: {
                // Steer toward the waypoint after the current one, so the walk rounds the corner early.
                const WalkPoint aim = point_at(next_ + 1 < count ? next_ + 1 : next_);
                heading_ = static_cast<std::int16_t>(heading_to(a, aim));
                heading_ = static_cast<std::int16_t>(unsigned_angle(heading_));
                const std::int16_t turn = shortest_turn(a.yaw, heading_);
                const std::int32_t rate = turn < kSlowTurnLimit ? kSlowTurn : kFastTurn;
                const std::int32_t turn_step = (rate * delta_time) / kFrame30;
                if (facing(turn)) {
                    a.yaw = heading_;
                } else if (turn < 0) {
                    a.yaw = static_cast<std::int16_t>(a.yaw - turn_step);
                } else {
                    a.yaw = static_cast<std::int16_t>(a.yaw + turn_step);
                }
                a.yaw = static_cast<std::int16_t>(unsigned_angle(a.yaw));

                std::int32_t distance = distance_to(a, point_at(next_));
                const std::int32_t reach = (((walk_anim - 1) * kReachPerAnim) + kReachPerAnim) >> 4;
                // A waypoint is passed when it is close enough, or when the walk starts moving away from it
                // (the last waypoint must be reached by distance).
                if (distance < reach || (last_distance_ < distance && next_ != count - 1)) {
                    ++next_;
                    distance = distance_to(a, point_at(next_));
                    a.phase = next_ == count ? 6 : 4;
                }
                last_distance_ = distance;
                break;
            }

            case 6: {
                const std::int16_t turn = shortest_turn(a.yaw, static_cast<std::int16_t>(end_heading));
                if (facing(turn)) {
                    a.yaw = static_cast<std::int16_t>(end_heading);
                    a.phase = 8;
                } else {
                    turn_on_spot(a, turn);
                    a.phase = 7;
                }
                break;
            }

            case 7:
                if (facing(shortest_turn(a.yaw, static_cast<std::int16_t>(end_heading)))) {
                    a.anim_request = walk_anim - 1;
                    a.yaw = static_cast<std::int16_t>(end_heading);
                    a.anim = 0;
                    a.restart_state = true;
                    a.phase = 8;
                }
                break;

            case 8:
                a.arrived = 1;
                heading_ = 0;
                a.phase = 0;
                a.stop_motion = true;
                return true;

            default:
                break;
        }
        return false;
    }

    /* The signed turn from `from` to `to` (both Q12 angles) the short way round, as the game computes it in
     * 16 bits. */
    static std::int16_t shortest_turn(std::int16_t from, std::int16_t to) {
        const std::int32_t f = from;
        const std::int32_t t = to;
        if (t > f) {
            if (t - f < 2048) {
                return static_cast<std::int16_t>(t - f);
            }
            return static_cast<std::int16_t>(static_cast<std::int16_t>(t - 4096) - f);
        }
        if (f - t < 2048) {
            return static_cast<std::int16_t>(t - f);
        }
        return static_cast<std::int16_t>(t - static_cast<std::int16_t>(f - 4096));
    }

private:
    static std::int32_t unsigned_angle(std::int32_t angle) { return (angle + 4096) & 0xFFF; }

    static bool facing(std::int16_t turn) {
        const std::int32_t t = turn;
        return (t < 0 ? -t : t) < kFacingTolerance;
    }

    static std::int32_t heading_to(const PathWalkActor& a, const WalkPoint& p) {
        return sh::world::fixed_atan2(sh::world::wrap_add(p.x, -a.x), sh::world::wrap_add(p.z, -a.z));
    }

    /* Planar distance in Q8: the offsets drop to Q8 first so the squares fit in 32 bits. */
    static std::int32_t distance_to(const PathWalkActor& a, const WalkPoint& p) {
        const std::int32_t dx = sh::world::wrap_add(a.x, -p.x) >> 4;
        const std::int32_t dz = sh::world::wrap_add(a.z, -p.z) >> 4;
        return sh::world::game_sqrt(sh::world::wrap_add(sh::world::wrap_mul(dx, dx), sh::world::wrap_mul(dz, dz)));
    }

    static void turn_on_spot(PathWalkActor& a, std::int16_t turn) {
        const std::int32_t anim = turn < 0 ? kTurnLeftAnim : kTurnRightAnim;
        a.anim_request = anim;
        a.anim = anim;
        a.restart_state = true;
    }

    std::int16_t next_ = 0;
    std::int16_t heading_ = 0;
    std::int32_t last_distance_ = 0;
};

}  // namespace sh::maps
