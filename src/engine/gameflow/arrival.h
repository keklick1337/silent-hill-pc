#pragma once

// Arrival after a room or map change: a door event names the
// map point the player arrives at. A point with half_width_q4 == 1 keeps the player's offset from the door
// he used (walk-through passages); otherwise the player stands on the point. The pose faces the
// point's Q8 angle (heading_q8), and a point with half_width_q4 >= 2 puts the player 0.4 m ahead of it.

#include <cstdint>

namespace sh::gameflow {

struct ArrivalPoint {
    std::int32_t x = 0;  // Q12
    std::int32_t z = 0;  // Q12
    std::uint8_t heading_q8 = 0;     // Q8 angle (256 per turn)
    std::uint8_t half_width_q4 = 0;  // the point's trigger half width (1/16 m); 1 = keep the offset from the door,
                                     // >= 2 = step 0.4 m ahead
};

struct ArrivalPose {
    std::int32_t x = 0;        // Q12; the height is 0 until the ground is queried
    std::int32_t z = 0;
    std::int16_t heading = 0;  // Q12 angle
};

// The arrival point for a player standing at (player_x, player_z) who used a door at (door_x,
// door_z).
ArrivalPoint arrival_point(const ArrivalPoint& target, std::int32_t door_x, std::int32_t door_z, std::int32_t player_x,
                           std::int32_t player_z);

ArrivalPose arrival_pose(const ArrivalPoint& point);

}  // namespace sh::gameflow
