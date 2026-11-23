#include "engine/devtools/free_camera.h"

#include "engine/world/fixed_trig.h"

#include <algorithm>
#include <cmath>

namespace sh::devtools {

namespace {

constexpr int kMoveSpeed = 640;  // Q12 units per 60 fps frame at x1
constexpr int kRiseSpeed = 384;
constexpr int kMouseYaw = 6;     // Q12 angle units per mouse pixel
constexpr int kMousePitch = 4;
constexpr int kStickDeadZone = 24;
constexpr int kStickYaw = 40;    // the same feel as the alternate camera on a pad
constexpr int kStickPitch = 28;
constexpr int kPitchMax = 900;   // about 79 degrees either way
constexpr std::int32_t kStep30Fps = 136;  // Q12 seconds of a 30 fps frame
constexpr float kEaseMove = 0.12f;
constexpr float kEaseSteady = 0.60f;
constexpr float kEaseLook = 0.25f;
constexpr float kSteadyScale = 0.25f;
constexpr float kSpeedMin = 1.0f / 32.0f;
constexpr float kSpeedMax = 32.0f;
constexpr std::int32_t kLookDistance = 20480;  // 5 units

int dead_zone(int raw) {
    const int v = raw - 128;
    return (v > -kStickDeadZone && v < kStickDeadZone) ? 0 : v;
}

// A per-30-fps-frame amount scaled to this frame's step.
std::int32_t per_frame(std::int32_t amount, std::int32_t delta_time) {
    return (amount * delta_time) / kStep30Fps;
}

}  // namespace

void FreeCamera::start(const std::array<std::int32_t, 3>& start_position, std::int32_t start_yaw) {
    position = start_position;
    yaw = start_yaw;
    pitch = 0;
    yaw_f_ = static_cast<float>(yaw & 0xFFF);
    pitch_f_ = 0.0f;
    velocity_ = {};
    remainder_ = {};
    look_rate_ = {};
    key_was_down_ = {true, true};
    cruise = false;
    steady = false;
}

void FreeCamera::aim(double yaw_units, double pitch_units) {
    yaw_f_ = static_cast<float>(std::fmod(std::fmod(yaw_units, 4096.0) + 4096.0, 4096.0));
    pitch_f_ = static_cast<float>(std::clamp(pitch_units, -static_cast<double>(kPitchMax), static_cast<double>(kPitchMax)));
    yaw = static_cast<std::int32_t>(yaw_f_) & 0xFFF;
    pitch = static_cast<std::int32_t>(pitch_f_);
    track_yaw_ = yaw_f_;
    track_pitch_ = pitch_f_;
    look_rate_ = {};
}

bool FreeCamera::edge(bool down, int slot) {
    const bool hit = down && !key_was_down_[slot];
    key_was_down_[slot] = down;
    return hit;
}

void FreeCamera::update(const FreeCameraInput& in) {
    float dt = static_cast<float>(in.delta_time) / 4096.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;

    if (edge(in.keys.hud, 0)) {
        hud_hidden = !hud_hidden;
    }
    if (edge(in.keys.cruise, 1)) {
        cruise = !cruise;
        cruise_velocity_ = velocity_;
    }

    // Steady keeps the travel direction of the moment it was pressed, so a dolly holds its line.
    if (in.keys.steady && !steady) {
        track_yaw_ = yaw_f_;
        track_pitch_ = pitch_f_;
    }
    steady = in.keys.steady;

    for (int n = in.wheel_notches; n > 0; n--) speed_scale *= 1.25f;
    for (int n = in.wheel_notches; n < 0; n++) speed_scale /= 1.25f;
    if (speed_scale < kSpeedMin) speed_scale = kSpeedMin;
    if (speed_scale > kSpeedMax) speed_scale = kSpeedMax;
    // Snap near 1 so the panel reads x1.00 again after scrolling back.
    if (speed_scale > 0.99f && speed_scale < 1.01f) speed_scale = 1.0f;

    float d_yaw = static_cast<float>(in.mouse_dx) * kMouseYaw * in.mouse_sensitivity;
    float d_pitch = static_cast<float>(in.mouse_dy) * kMousePitch * in.mouse_sensitivity * (in.invert_mouse_y ? -1.0f : 1.0f);

    if (in.pad.present) {
        const int rx = dead_zone(in.pad.right_x);
        const int ry = dead_zone(in.pad.right_y);
        if (rx != 0 || ry != 0) {
            const float s = in.pad_sensitivity;
            d_yaw += static_cast<float>(per_frame(static_cast<std::int32_t>(((rx * kStickYaw) >> 7) * s), in.delta_time));
            d_pitch += static_cast<float>(per_frame(static_cast<std::int32_t>(((ry * kStickPitch) >> 7) * s), in.delta_time)) *
                       (in.invert_pad_y ? -1.0f : 1.0f);
        }
    }

    if (steady && dt > 0.0f) {
        const float k = 1.0f - std::exp(-dt / kEaseLook);
        look_rate_[0] += (d_yaw * kSteadyScale / dt - look_rate_[0]) * k;
        look_rate_[1] += (d_pitch * kSteadyScale / dt - look_rate_[1]) * k;
        d_yaw = look_rate_[0] * dt;
        d_pitch = look_rate_[1] * dt;
    } else {
        look_rate_[0] = (dt > 0.0f) ? d_yaw / dt : 0.0f;
        look_rate_[1] = (dt > 0.0f) ? d_pitch / dt : 0.0f;
    }
    yaw_f_ += d_yaw;
    while (yaw_f_ >= 4096.0f) yaw_f_ -= 4096.0f;
    while (yaw_f_ < 0.0f) yaw_f_ += 4096.0f;
    pitch_f_ += d_pitch;
    if (pitch_f_ > kPitchMax) pitch_f_ = kPitchMax;
    if (pitch_f_ < -kPitchMax) pitch_f_ = -kPitchMax;
    yaw = static_cast<std::int32_t>(yaw_f_) & 0xFFF;
    pitch = static_cast<std::int32_t>(pitch_f_);

    float speed = static_cast<float>(kMoveSpeed) * 60.0f * speed_scale;
    if (in.keys.fast) speed *= 4.0f;
    if (in.keys.slow) speed *= 0.25f;
    if (in.pad.present && in.pad.fast) speed *= 4.0f;
    if (in.pad.present && in.pad.slow) speed *= 0.25f;
    if (steady) speed *= kSteadyScale;
    const float rise_speed = speed * (static_cast<float>(kRiseSpeed) / static_cast<float>(kMoveSpeed));

    // Forward follows the view, pitch included.
    const float yaw_r = (steady ? track_yaw_ : yaw_f_) * (6.2831853f / 4096.0f);
    const float pitch_r = (steady ? track_pitch_ : pitch_f_) * (6.2831853f / 4096.0f);
    const float fwd[3] = {std::sin(yaw_r) * std::cos(pitch_r), std::sin(pitch_r), std::cos(yaw_r) * std::cos(pitch_r)};
    const float side[3] = {std::cos(yaw_r), 0.0f, -std::sin(yaw_r)};

    float f = static_cast<float>(static_cast<int>(in.keys.forward) - static_cast<int>(in.keys.back));
    float r = static_cast<float>(static_cast<int>(in.keys.right) - static_cast<int>(in.keys.left));
    float vert = static_cast<float>(static_cast<int>(in.keys.sink) - static_cast<int>(in.keys.rise));
    if (in.pad.present) {
        const int lx = dead_zone(in.pad.left_x);
        const int ly = dead_zone(in.pad.left_y);
        f += static_cast<float>(-ly) / 128.0f;
        r += static_cast<float>(lx) / 128.0f;
        if (in.pad.rise) vert -= 1.0f;
        if (in.pad.sink) vert += 1.0f;
    }
    float target[3];
    for (int i = 0; i < 3; i++) {
        target[i] = (fwd[i] * f + side[i] * r) * speed;
    }
    target[1] += vert * rise_speed;
    const bool moving = f != 0.0f || r != 0.0f || vert != 0.0f;

    // Cruise keeps the last velocity; any move input aims it again.
    if (cruise) {
        if (moving) {
            for (int i = 0; i < 3; i++) cruise_velocity_[i] = target[i];
        }
        for (int i = 0; i < 3; i++) target[i] = cruise_velocity_[i];
    }

    const float k = (dt > 0.0f) ? 1.0f - std::exp(-dt / (steady ? kEaseSteady : kEaseMove)) : 0.0f;
    for (int i = 0; i < 3; i++) {
        velocity_[i] += (target[i] - velocity_[i]) * k;
        // The fraction is carried: a slow camera at a high frame rate moves less than a unit a frame.
        const float travel = velocity_[i] * dt + remainder_[i];
        const auto whole = static_cast<std::int32_t>(travel);
        position[i] += whole;
        remainder_[i] = travel - static_cast<float>(whole);
    }
}

std::array<double, 3> FreeCamera::exact_position() const {
    return {position[0] + static_cast<double>(remainder_[0]), position[1] + static_cast<double>(remainder_[1]),
            position[2] + static_cast<double>(remainder_[2])};
}

std::array<std::int32_t, 3> FreeCamera::look_at() const {
    const std::int32_t sin_y = world::fixed_sin(yaw);
    const std::int32_t cos_y = world::fixed_cos(yaw);
    const auto ahead = static_cast<std::int32_t>(static_cast<std::int64_t>(kLookDistance) * world::fixed_cos(pitch) >> 12);
    return {position[0] + static_cast<std::int32_t>(static_cast<std::int64_t>(ahead) * sin_y >> 12),
            position[1] + static_cast<std::int32_t>(static_cast<std::int64_t>(kLookDistance) * world::fixed_sin(pitch) >> 12),
            position[2] + static_cast<std::int32_t>(static_cast<std::int64_t>(ahead) * cos_y >> 12)};
}

}  // namespace sh::devtools
