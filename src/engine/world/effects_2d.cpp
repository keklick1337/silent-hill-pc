// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/world/effects_2d.h"

namespace sh::world {

namespace {

std::int32_t rotate_row(const math::Matrix3& m, int row, std::int32_t x, std::int32_t y, std::int32_t z) {
    const std::int64_t sum = static_cast<std::int64_t>(m.m[row][0]) * x + static_cast<std::int64_t>(m.m[row][1]) * y +
                             static_cast<std::int64_t>(m.m[row][2]) * z;
    return static_cast<std::int32_t>(sum >> 12);
}

std::array<float, 3> to_view_point(const ViewMatrix& view, const math::Vec3i& world_q12) {
    const std::int32_t x = world_q12.x >> 4, y = world_q12.y >> 4, z = world_q12.z >> 4;  // Q12 to Q8
    const auto add = [](std::int32_t a, std::int32_t b) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
    };
    return {static_cast<float>(add(rotate_row(view.rot, 0, x, y, z), view.trans.x)),
            static_cast<float>(add(rotate_row(view.rot, 1, x, y, z), view.trans.y)),
            static_cast<float>(add(rotate_row(view.rot, 2, x, y, z), view.trans.z))};
}

}  // namespace

FlashlightCone flashlight_cone(const FlashlightConeIn& in) {
    FlashlightCone out;
    out.position = to_view_point(in.view, in.light_position);
    out.shadow_position = to_view_point(in.view, in.shadow_position);
    // The beam direction is rotated only.
    const std::int32_t dx = in.direction.x, dy = in.direction.y, dz = in.direction.z;
    out.direction = {static_cast<float>(rotate_row(in.view.rot, 0, dx, dy, dz)),
                     static_cast<float>(rotate_row(in.view.rot, 1, dx, dy, dz)),
                     static_cast<float>(rotate_row(in.view.rot, 2, dx, dy, dz))};
    if (in.tinted_by_room) {
        out.color = {static_cast<float>(in.color_matrix.m[0][0]) / 4096.0f,
                     static_cast<float>(in.color_matrix.m[1][0]) / 4096.0f,
                     static_cast<float>(in.color_matrix.m[2][0]) / 4096.0f};
    } else {
        out.color = {1.00f, 0.95f, 0.85f};
    }
    return out;
}

bool StableFlag::update(bool raw_flag) {
    const int raw = raw_flag ? 1 : 0;
    if (state_ < 0) {
        state_ = raw;
        candidate_ = raw;
        agree_ = 0;
    } else if (raw != candidate_) {
        candidate_ = raw;
        agree_ = 0;
    } else if (raw != state_ && ++agree_ >= frames_) {
        state_ = raw;
        agree_ = 0;
    }
    return state_ != 0;
}

std::uint32_t overlay_gray(std::int32_t brightness) {
    const std::uint32_t b = static_cast<std::uint32_t>(brightness);
    return b + (b << 8) + (b << 16);
}

}  // namespace sh::world
