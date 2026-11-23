#pragma once

// The loading screen's running Harry: where the two camera
// targets sit relative to the player and the fixed pose and neutral light the screen forces. The numbers
// are gamedata/config/load_screen.json; the host applies the result to its camera and animation state.

#include <cstdint>
#include <string_view>

#include "formats/common/status.h"

namespace sh::gameflow {

struct LoadScreenParams {
    std::int32_t look_height = -2457;        // Q12 height of the watch target
    std::int32_t cam_height = -4096;         // Q12 height of the camera target
    std::int32_t cam_angle_offset = -256;    // Q12 angle added to the player yaw before the camera offset
    std::int32_t cam_distance_mul = 2;       // camera offset = sin/cos of that angle times this
    std::int32_t pose_time = 106496;         // Q12 animation time the run starts at
    std::int32_t player_height = 819;        // Q12 player height above the floor
    std::int32_t light_tint = 4096;          // neutral light tint (Q7 value << 5)

    static LoadScreenParams builtin() { return {}; }
    static Result<LoadScreenParams> parse(std::string_view json);
};

struct LoadScreenTarget {
    std::int32_t x = 0, y = 0, z = 0;
};

struct LoadScreenView {
    LoadScreenTarget look_at;
    LoadScreenTarget cam_target;
};

// Both targets from the player's position and yaw (Q12 angle).
LoadScreenView load_screen_view(const LoadScreenParams& p, std::int32_t px, std::int32_t pz, std::int32_t yaw);

}  // namespace sh::gameflow
