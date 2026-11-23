#include "engine/gameflow/arrival.h"

#include "engine/world/fixed_trig.h"

namespace sh::gameflow {

namespace {

constexpr std::int32_t kStepAhead = 1638;  // 0.4 m in Q12, truncated

constexpr std::int32_t wrap_add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
constexpr std::int32_t wrap_sub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

}  // namespace

ArrivalPoint arrival_point(const ArrivalPoint& target, std::int32_t door_x, std::int32_t door_z, std::int32_t player_x,
                           std::int32_t player_z) {
    ArrivalPoint out = target;
    if (target.half_width_q4 == 1) {
        out.x = wrap_add(out.x, wrap_sub(player_x, door_x));
        out.z = wrap_add(out.z, wrap_sub(player_z, door_z));
    }
    return out;
}

ArrivalPose arrival_pose(const ArrivalPoint& point) {
    ArrivalPose pose;
    pose.heading = static_cast<std::int16_t>(point.heading_q8 << 4);
    pose.x = point.x;
    pose.z = point.z;
    if (point.half_width_q4 >= 2) {
        pose.x += static_cast<std::int32_t>((static_cast<std::int64_t>(sh::world::fixed_sin(pose.heading)) * kStepAhead) >> 12);
        pose.z += static_cast<std::int32_t>((static_cast<std::int64_t>(sh::world::fixed_cos(pose.heading)) * kStepAhead) >> 12);
    }
    return pose;
}

}  // namespace sh::gameflow
