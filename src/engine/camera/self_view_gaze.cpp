#include "engine/camera/self_view_gaze.h"

#include "engine/camera/camera_math.h"

namespace sh::camera {

namespace {

using fx::angle;

// The angles are 16-bit like the bone angles they start from: every step wraps to 16 bits.
constexpr std::int16_t s16(std::int32_t v) { return static_cast<std::int16_t>(v); }

}  // namespace

Angles self_view_gaze(const SelfViewGazeInput& in, const SelfViewGazeAnims& anims) {
    const std::int32_t status = in.anim_status;
    const auto plays = [status](int anim) {
        const std::int32_t packed = static_cast<std::int32_t>(static_cast<std::uint32_t>(anim) << 1);
        return status == packed || status == (packed | 1);
    };
    const bool running = plays(anims.run_forward);
    const bool looking_around = plays(anims.look_around);
    const bool idle = plays(anims.idle);
    const bool turning = plays(anims.turn_left) || plays(anims.turn_right);
    const bool walking = plays(anims.walk_forward) || running || turning;

    Angles head = in.head;
    const std::int32_t head_turn = fx::wrap_angle(head.y - in.body_yaw);

    // Roll: kept while running, dropped while looking around at an enemy, halved otherwise.
    if (running) {
    } else if (looking_around) {
        if (in.enemy_near) {
            head.z = 0;
        }
    } else {
        head.z = s16(head.z >> 1);
    }

    // Yaw: a fall keeps the bone's; idle lets the head lead the body a little past 4 degrees,
    // walking eases it within 10 degrees of the body.
    if (plays(anims.fall_forward) || plays(anims.fall_backward)) {
    } else if (looking_around) {
        head.y = in.enemy_near ? in.body_yaw : s16(head.y + angle(30.0f));
    } else if (idle) {
        const std::int32_t magnitude = fx::abs32(head_turn);
        std::int32_t turn = head_turn;
        if (magnitude > angle(4.0f)) {
            turn = ((magnitude - angle(4.0f)) >> 3) + angle(4.0f);
            if (head_turn < 0) {
                turn = -turn;
            }
        }
        head.y = s16(in.body_yaw + turn);
    } else if (walking) {
        head.y = s16(in.body_yaw + fx::clamp(head_turn >> 3, angle(-10.0f), angle(10.0f)));
    } else {
        head.y = in.body_yaw;
    }

    // Pitch. Only the inactive walk status keeps the bone's own.
    const std::int32_t walk_inactive = static_cast<std::int32_t>(static_cast<std::uint32_t>(anims.walk_forward) << 1);
    if (status == walk_inactive) {
    } else if (plays(anims.idle_exhausted) || idle) {
        if (plays(anims.idle_exhausted)) {
            head.x = s16(head.x >> 1);
        }
        head.x = s16(head.x - angle(8.0f));
    } else if (looking_around) {
        head.x = in.enemy_near ? s16(angle(-7.0f)) : s16(head.x - angle(8.0f));
    } else if (turning) {
        const std::int32_t heading_sector = (in.body_yaw >> 7) & 0xF;
        if (heading_sector == 0 || heading_sector == 5) {
            head.x = s16(head.x - angle(1.0f));
        }
        head.x = s16(head.x - angle(6.0f));
    }

    // The pitch toward the look-at: on top while moving or idle, scaled alone otherwise.
    if (walking || looking_around || idle) {
        head.x = s16(head.x + (in.pitch >> 1));
    } else {
        head.x = s16(fx::mul(in.pitch, angle(252.0f)));
    }
    return head;
}

}  // namespace sh::camera
