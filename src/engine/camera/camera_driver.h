#pragma once

// The camera driver's rules: where the camera starts when a map
// begins, how the followed character becomes the camera's subject each frame, the head point of
// a head bone, the developer camera modes (orbit around a reference, fly by pad) and the
// listener direction of positional sound. Pure integer functions on engine types; the game bridge
// (camera_entry.cpp, over camera_host.h) supplies the character, the world's ground,
// the pad and the view frames.

#include <cstdint>

#include "engine/camera/camera_config.h"
#include "engine/camera/camera_rig.h"
#include "engine/camera/camera_types.h"

namespace sh::camera {

// The camera position at map start: `distance` behind a character at `pos` facing `yaw`, and
// `height` above its feet (Q12; +y is down).
Vec3 first_camera_position(const Vec3& pos, std::int16_t yaw, const CameraConfig& config);

// `pos` moved by `xz_radius` along `yaw` on the ground plane and by `dy` vertically.
Vec3 offset_position(const Vec3& pos, std::int32_t xz_radius, std::int32_t yaw, std::int32_t dy);

// The head point of a head bone: the rule's offset in the bone's frame (Q8), through the bone's
// world matrix (Q8 translation), as Q12 world coordinates raised by head_offset_y.
Vec3 head_point(const Matrix& head_world, const SubjectRules& rules);

// One frame of the followed character as the driver reads it.
struct SubjectSource {
    Vec3 position{};
    bool connected = false;          // riding a scripted connection: fixed ground and head
    std::int32_t ground_height = 0;  // the world's ground under the character (not connected)
    Vec3 head{};                     // its head point (not connected)
    std::int32_t elevation_rate = 0; // Q12 0..1: how much the subject's bottom drops below its feet
    std::int32_t move_speed = 0;
    std::int32_t heading = 0;
    std::int32_t turn_speed = 0;
    std::int32_t yaw = 0;
};

struct SubjectFrame {
    Subject subject{};
    bool inhibit_far_look = false;  // the camera must not far-look while the bottom is lowered
};

SubjectFrame subject_of(const SubjectSource& src, const SubjectRules& rules);

// ---- the developer camera ----

// The orbit of the reference camera (the game keeps it in its system record): XZ radius, yaw and
// height, Q12.
struct Orbit {
    std::int32_t radius = 0;
    std::int32_t yaw = 0;
    std::int32_t height = 0;
};

struct OrbitInput {
    bool closer = false;
    bool farther = false;
    bool turn_right = false;
    bool turn_left = false;
    bool up = false;
    bool down = false;
};

// One frame of orbit editing; returns the orbit centre (ahead of and above the subject at
// `subject_pos` facing `subject_yaw`).
Vec3 adjust_orbit(Orbit& orbit, const OrbitInput& in, std::int32_t vblanks, const Vec3& subject_pos,
                  std::int32_t subject_yaw, const DebugCameraRules& rules);

struct FlyInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool forward = false;   // fly ahead
    bool backward = false;  // fly back
    bool shift = false;     // stick moves the camera instead of turning it
    bool boost = false;
};

struct FlyResult {
    Matrix view{};          // the camera matrix (rotation and Q8 position) for the view system
    bool moved = false;     // any movement input: the orbit follows the flying camera
    Vec3 reference{};       // then the new orbit centre (Q12)
    Orbit orbit{};          // and the orbit that reaches the camera from there
};

// One frame of the fly camera from the current view position (Q12) and angles.
FlyResult fly_by_pad(const Vec3& view_pos, const Angles& view_angles, const FlyInput& in, std::int32_t vblanks,
                     const DebugCameraRules& rules);

// ---- positional sound ----

// Unit direction (Q12) from the listener to a sound source, from their difference in Q6.
Vec3 listener_direction(const Vec3& listener, const Vec3& source);

}  // namespace sh::camera
