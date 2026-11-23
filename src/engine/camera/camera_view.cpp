// Stage C of the camera update (see camera_rig.h): where the camera looks, its rotation and matrix,
// what the view system gets, and the end-of-frame bookkeeping. The arithmetic is the shipped
// game's, reproduced exactly.

#include "engine/camera/camera_math.h"
#include "engine/camera/camera_matrix.h"
#include "engine/camera/camera_motion.h"
#include "engine/camera/camera_rig.h"

namespace sh::camera {

namespace {

using fx::abs32;
using fx::angle;
using fx::clamp;
using fx::from_q4;
using fx::mul;
using fx::mul_wide;
using fx::q12;
using fx::wrap_angle;

bool is_move(std::int32_t move_type, CameraMove move) { return move_type == static_cast<std::int32_t>(move); }

std::int16_t s16(std::int32_t v) { return static_cast<std::int16_t>(v); }

// Head-bob of the self view: the mean of two cosines of the timer at two rates, scaled by `width`.
// The rates are radians in a 10240-per-half-turn unit.
std::int32_t head_bob(std::int32_t width, std::int32_t rate_a, std::int32_t rate_b, std::int32_t timer) {
    const std::int32_t wave = (fx::cos(mul_wide(rate_a, timer)) + fx::cos(mul_wide(rate_b, timer))) >> 1;
    return mul(width, wave);
}

// The settle camera's base pitch by the pitch toward the look-at (0..90 degrees over 0..1024).
constexpr std::int32_t kSettlePitch[] = {angle(0.0f), angle(15.0f), angle(60.0f), angle(110.0f), angle(110.0f)};

class ViewStep {
public:
    ViewStep(CameraState& st, const FrameInput& in, const CameraRig& rig) : st_(st), in_(in), rig_(rig) {}

    StageCResult run(const StageAResult& a, const StageBResult& b, const Vec3& start_pos, const Angles& start_rot) {
        const CameraRoad& road = rig_.road(st_.current_road.road);
        if ((st_.flags & camera_flags::user_watch) == 0) {
            update_look_at(a.move_type, road.area_size, a.far_look_rate, a.self_view_rate);
            if ((road.flags & road_flags::limit_far_look_up) != 0 &&
                (road.camera_move == CameraMove::Chase || is_move(a.move_type, CameraMove::SelfView))) {
                limit_far_look_up();
            }
        }
        widen_shot();
        update_rotation(b.watch_params, a.move_type, (st_.flags & camera_flags::character_visible) != 0);

        StageCResult result;
        result.view = view_output(a.move_type);

        st_.through_door_activate = false;
        st_.flags &= ~(camera_flags::warp_camera | camera_flags::warp_watch | camera_flags::warp_camera_target);

        result.smooth_move_type = smooth_move(start_pos, start_rot);
        return result;
    }

private:
    std::int32_t dt() const { return in_.delta_time; }

    // --- look-at target ---

    Vec3 far_look_at(AreaSize area) const {
        std::int32_t reach;
        if (area == AreaSize::Tiny) {
            reach = q12(2.8f);
        } else if (area == AreaSize::Small) {
            reach = q12(5.5f);
        } else {
            reach = q12(10.0f);
        }
        std::int32_t look_y = st_.chara_pos.y - q12(0.8f);

        if (st_.nearest_enemy >= 0 && static_cast<std::size_t>(st_.nearest_enemy) < in_.npcs.size()) {
            const NpcView& enemy = in_.npcs[static_cast<std::size_t>(st_.nearest_enemy)];
            std::int32_t distance = st_.nearest_enemy_xz_distance;
            const std::int32_t pull = distance < q12(1.7f) ? fx::wrap_mul(distance, q12(-0.7f)) / q12(1.7f) : q12(-0.7f);
            distance += pull;
            if (distance < reach) {
                reach = distance;
            }
            if (reach < q12(0.4f)) {
                reach = q12(0.4f);
            }

            switch (enemy.look_anchor) {
                case NpcView::LookAnchor::Ground: {
                    const GroundSample ground = in_.ground_at ? in_.ground_at(enemy.position.x, enemy.position.z)
                                                              : GroundSample{};
                    look_y = (ground.found ? ground.height : enemy.position.y) + enemy.look_offset_y;
                    break;
                }
                case NpcView::LookAnchor::Camera:
                    look_y = st_.watch_position_y + enemy.look_offset_y;
                    break;
                default:
                    look_y = enemy.position.y + enemy.look_offset_y;
                    break;
            }

            const std::int32_t rise_limit = (st_.nearest_enemy_xz_distance >> 1) - q12(0.5f);
            if (st_.chara_pos.y + rise_limit < look_y) {
                look_y = st_.chara_pos.y + rise_limit;
            }
        }

        return Vec3{st_.chara_pos.x + mul(reach, fx::sin(st_.chara_eye_angle_y)), look_y,
                    st_.chara_pos.z + mul(reach, fx::cos(st_.chara_eye_angle_y))};
    }

    void normal_look_at(std::int32_t move_type, AreaSize area) {
        st_.watch_target_roll = 0;
        const CameraRoad& road = rig_.road(st_.current_road.road);

        if (is_move(move_type, CameraMove::FixedAngle)) {
            // A fixed shot looks along its authored direction, a quarter metre ahead of the camera.
            const std::int32_t pitch = wrap_angle(s16(road.fix_angle_x << 4));
            const std::int32_t yaw = wrap_angle(s16(road.fix_angle_y << 4));
            const std::int32_t reach = q12(0.25f);
            const std::int32_t planar = mul(reach, fx::cos(pitch));
            const std::int16_t ahead_y = s16(mul(-reach, fx::sin(pitch)));
            const std::int16_t ahead_x = s16(mul(planar, fx::sin(yaw)));
            const std::int16_t ahead_z = s16(mul(planar, fx::cos(yaw)));
            st_.watch_target_pos.x = (ahead_x << 4) + st_.cam_pos.x;
            st_.watch_target_pos.y = (ahead_y << 4) + st_.cam_pos.y;
            st_.watch_target_pos.z = (ahead_z << 4) + st_.cam_pos.z;
            return;
        }

        const std::int32_t to_camera = fx::length(st_.chara_pos.x - st_.cam_pos.x, 0, st_.chara_pos.z - st_.cam_pos.z);
        std::int32_t circle_radius;
        std::int32_t circle_extra;
        if (area == AreaSize::Tiny || area == AreaSize::Small) {
            circle_radius = q12(1.0f);
            circle_extra = q12(3.0f);
        } else {
            circle_radius = q12(2.5f);
            circle_extra = q12(5.5f);
        }

        // A chasing camera too close to the character looks past it, away from the camera.
        std::int32_t lead = 0;
        if (is_move(move_type, CameraMove::Chase) || is_move(move_type, CameraMove::ThroughDoor)) {
            if (to_camera < circle_radius + circle_extra) {
                lead = (circle_radius + circle_extra) - to_camera;
            }
        }

        const std::int32_t look_y = from_q4(road.watch_offset_y) + st_.chara_bottom_y;
        const std::int16_t camera_to_chara =
            s16(fx::atan2(st_.chara_pos.x - st_.cam_pos.x, st_.chara_pos.z - st_.cam_pos.z));
        st_.watch_target_pos.x = st_.chara_pos.x + mul_wide(lead, fx::sin(camera_to_chara)) +
                                 mul_wide(circle_radius, fx::sin(st_.chara_eye_angle_y));
        st_.watch_target_pos.z = st_.chara_pos.z + mul_wide(lead, fx::cos(camera_to_chara)) +
                                 mul_wide(circle_radius, fx::cos(st_.chara_eye_angle_y));
        st_.watch_target_pos.y = is_move(move_type, CameraMove::SelfView) ? st_.chara_center_y : look_y;
    }

    // Blends the look-at toward where the subject's head looks, by the self-view rate.
    void blend_self_view(std::int32_t rate) {
        const std::int32_t dx = st_.watch_target_pos.x - st_.cam_pos.x;
        const std::int32_t dy = st_.watch_target_pos.y - st_.cam_pos.y;
        const std::int32_t dz = st_.watch_target_pos.z - st_.cam_pos.z;
        const std::int32_t reach = fx::length(dx, dy, dz);
        std::int32_t pitch = fx::atan2(-dy, fx::length(dx, 0, dz));
        pitch = clamp(pitch, angle(-70.0f), angle(70.0f));

        GazeQuery query;
        query.pitch = pitch;
        query.enemy_near = st_.nearest_enemy >= 0;
        Angles gaze = in_.self_view_gaze ? in_.self_view_gaze(query) : Angles{};
        gaze.x = s16(clamp(gaze.x, angle(-80.0f), angle(80.0f)));

        st_.watch_target_roll = s16(st_.watch_target_roll + mul_wide(wrap_angle(gaze.z - st_.watch_target_roll), rate));

        const std::int32_t gx = st_.cam_pos.x + mul(fx::cos(gaze.x), mul(reach, fx::sin(gaze.y)));
        const std::int32_t gz = st_.cam_pos.z + mul(fx::cos(gaze.x), mul(reach, fx::cos(gaze.y)));
        const std::int32_t gy = st_.cam_pos.y - mul(reach, fx::sin(gaze.x));
        st_.watch_target_pos.x += mul_wide(gx - st_.watch_target_pos.x, rate);
        st_.watch_target_pos.y += mul_wide(gy - st_.watch_target_pos.y, rate);
        st_.watch_target_pos.z += mul_wide(gz - st_.watch_target_pos.z, rate);
    }

    void update_look_at(std::int32_t move_type, AreaSize area, std::int32_t far_rate, std::int32_t self_rate) {
        const Vec3 far = far_look_at(area);
        if (!is_move(move_type, CameraMove::SelfView)) {
            normal_look_at(move_type, area);
            if (far_rate != 0) {
                st_.watch_target_pos.x += mul(far_rate, far.x - st_.watch_target_pos.x);
                st_.watch_target_pos.y += mul(far_rate, far.y - st_.watch_target_pos.y);
                st_.watch_target_pos.z += mul(far_rate, far.z - st_.watch_target_pos.z);
            }
        } else {
            st_.watch_target_pos = far;
        }
        blend_self_view(s16(self_rate));
        if (st_.watch_target_pos.y > st_.watch_target_max_y) {
            st_.watch_target_pos.y = st_.watch_target_max_y;
        }
    }

    // Keeps a far look from tilting the camera up past the top of the authored scene.
    void limit_far_look_up() {
        const std::int16_t max_pitch =
            s16(fx::atan2(st_.cam_pos.y + q12(5.0f), q12(13.0f)) - fx::atan2(in_.screen_height / 2, st_.geom_screen_dist));
        const std::int32_t reach =
            fx::length(st_.watch_target_pos.x - st_.cam_pos.x, 0, st_.watch_target_pos.z - st_.cam_pos.z);
        const std::int16_t pitch = s16(fx::atan2(-st_.watch_target_pos.y + st_.cam_pos.y, reach));
        if (pitch > max_pitch) {
            const std::int32_t rise = fx::wrap_mul((reach >> 4) * fx::sin(max_pitch) / fx::cos(max_pitch), 16);
            st_.watch_target_pos.y = st_.cam_pos.y - rise;
        }
    }

    void widen_shot() {
        const WideShot& w = in_.wide_shot;
        if (!w.enabled || abs32(st_.cam_pos.x - w.anchor_x) > w.tolerance ||
            abs32(st_.cam_pos.z - w.anchor_z) > w.tolerance) {
            return;
        }
        const std::int32_t dx = st_.watch_target_pos.x - st_.cam_pos.x;
        const std::int32_t dz = st_.watch_target_pos.z - st_.cam_pos.z;
        const std::int32_t reach = fx::length(dx, 0, dz);
        const std::int32_t yaw = (fx::atan2(dx, dz) + w.yaw_delta) & 0xFFF;
        st_.watch_target_pos.x = st_.cam_pos.x + mul_wide(reach, fx::sin(yaw));
        st_.watch_target_pos.z = st_.cam_pos.z + mul_wide(reach, fx::cos(yaw));
    }

    // --- rotation ---

    Angles new_base_rotation(std::int32_t move_type) const {
        if ((st_.flags & camera_flags::user_watch) != 0 || !is_move(move_type, CameraMove::Settle)) {
            return Angles{};
        }
        const std::int32_t dx = (st_.watch_target_pos.x - st_.cam_pos.x) >> 4;
        const std::int32_t dy = (st_.watch_target_pos.y - st_.cam_pos.y) >> 4;
        const std::int32_t dz = (st_.watch_target_pos.z - st_.cam_pos.z) >> 4;

        const std::int16_t pitch = s16(fx::atan2(-dy, fx::length(dx, 0, dz)));
        const std::int16_t yaw = s16(fx::atan2(dx, dz));

        // A settle zone keeps its yaw between two authored bounds.
        const CameraRoad& road = rig_.road(st_.current_road.road);
        const std::int16_t bound_a = s16(road.fix_angle_x << 4);
        const std::int16_t bound_b = s16(road.fix_angle_y << 4);
        const std::int16_t past_a = s16(wrap_angle(yaw - bound_a));
        const std::int16_t past_b = s16(wrap_angle(yaw - bound_b));
        std::int16_t base_yaw;
        if (past_a >= 0 && past_b <= 0) {
            base_yaw = yaw;
        } else if (abs32(past_a) < abs32(past_b)) {
            base_yaw = bound_a;
        } else {
            base_yaw = bound_b;
        }

        if ((st_.flags & camera_flags::warp_watch) == 0) {
            if (st_.chara_move_speed != 0 && pitch < angle(75.0f) && pitch > angle(-75.0f)) {
                const std::int16_t turn = s16(wrap_angle(base_yaw - st_.base_cam_rotation.y));
                const std::int16_t limit = s16(mul_wide(dt(), angle(120.0f)));
                base_yaw = s16(st_.base_cam_rotation.y + clamp(turn, -limit, limit));
            } else {
                base_yaw = st_.base_cam_rotation.y;
            }
        }

        std::int16_t base_pitch = s16(pitch < 0 ? -pitch : pitch);
        base_pitch = s16(piecewise_linear(kSettlePitch, 5, base_pitch, 0, q12(0.25f)));
        base_pitch = s16(clamp(base_pitch, 0, angle(90.0f)));
        if (pitch < 0) {
            base_pitch = s16(-base_pitch);
        }
        return Angles{base_pitch, base_yaw, 0};
    }

    // Moves the base rotation and re-expresses the offset rotation in the new base, so the view
    // does not jump.
    void rebase(const Angles& new_base) {
        const Matrix old_base = rotation_yxz(st_.base_cam_rotation);
        const Matrix new_base_inverse = transpose(rotation_yxz(new_base));
        const Matrix offset = rotation_yxz(st_.cam_rotation_offset);
        const Matrix change = multiply(new_base_inverse, old_base);
        st_.cam_rotation_offset = matrix_angles(multiply(change, offset));
        st_.base_cam_rotation = new_base;
    }

    // Angles of a world offset (Q12) seen from the camera base.
    static Angles base_relative_angles(const Matrix& base_inverse, std::int32_t dx, std::int32_t dy, std::int32_t dz) {
        const Angles offset{s16(dx >> 4), s16(dy >> 4), s16(dz >> 4)};
        return vector_angles(apply(base_inverse, offset));
    }

    // Turns the look offset so the character stays inside the screen.
    void keep_character_in_screen(Angles& look, const Angles& feet, const Angles& head) const {
        const std::int16_t feet_pitch = s16(wrap_angle(feet.x - look.x));
        const std::int16_t head_pitch = s16(wrap_angle(head.x - look.x));
        const std::int16_t head_yaw = s16(wrap_angle(head.y - look.y));
        const std::int16_t half_x = st_.screen_half_angle_x;
        const std::int16_t half_y = st_.screen_half_angle_y;

        const std::int16_t yaw_fix = s16(head_yaw > half_x ? head_yaw - half_x
                                                           : (-half_x > head_yaw ? half_x + head_yaw : 0));
        std::int16_t pitch_fix = s16(feet_pitch >= -half_y ? 0 : feet_pitch + half_y);
        if (half_y < head_pitch - pitch_fix) {
            pitch_fix = s16(head_pitch - half_y);
        }
        pitch_fix = s16(clamp(pitch_fix, angle(-30.0f), angle(30.0f)));

        look.y = s16(look.y + yaw_fix);
        look.x = s16(pitch_fix + look.x);
    }

    void ease_offset(const Angles& target, const WatchMoveParams& p) {
        Angles& offset = st_.cam_rotation_offset;
        Angles& speed = st_.cam_rotation_offset_speed;
        const std::int32_t brake_x = fx::mul_by(p.accel_x, 8.0f);
        const std::int32_t brake_y = fx::mul_by(p.accel_y, 3.0f);
        speed.x = s16(angular_speed_toward(speed.x, offset.x, target.x, p.accel_x, p.max_speed_x, brake_x, dt()));
        speed.y = s16(angular_speed_toward(speed.y, offset.y, target.y, p.accel_y, p.max_speed_y, brake_y, dt()));
        speed.z = s16(angular_speed_toward(speed.z, offset.z, target.z, q12(0.4f), angle(144.0f), q12(3.0f), dt()));
        offset.x = s16(offset.x + mul_wide(speed.x, dt()));
        offset.y = s16(offset.y + mul_wide(speed.y, dt()));
        offset.z = s16(offset.z + mul_wide(speed.z, dt()));
    }

    void update_rotation(const WatchMoveParams& params, std::int32_t move_type, bool character_visible) {
        const Angles new_base = new_base_rotation(move_type);
        if (new_base.x != st_.base_cam_rotation.x || new_base.y != st_.base_cam_rotation.y ||
            new_base.z != st_.base_cam_rotation.z) {
            rebase(new_base);
        }

        const Matrix base_inverse = transpose(rotation_yxz(st_.base_cam_rotation));
        Angles look = base_relative_angles(base_inverse, st_.watch_target_pos.x - st_.cam_pos.x,
                                           st_.watch_target_pos.y - st_.cam_pos.y,
                                           st_.watch_target_pos.z - st_.cam_pos.z);
        look.z = st_.watch_target_roll;

        if (character_visible) {
            const Angles feet = base_relative_angles(base_inverse, st_.chara_pos.x - st_.cam_pos.x,
                                                     st_.chara_bottom_y - st_.cam_pos.y,
                                                     st_.chara_pos.z - st_.cam_pos.z);
            const Angles head = base_relative_angles(base_inverse, st_.chara_pos.x - st_.cam_pos.x,
                                                     st_.chara_top_y - st_.cam_pos.y,
                                                     st_.chara_pos.z - st_.cam_pos.z);
            keep_character_in_screen(look, feet, head);
        }

        if ((st_.flags & camera_flags::warp_watch) != 0) {
            st_.cam_rotation_offset = look;
            st_.cam_rotation_offset_speed = Angles{};
        } else {
            ease_offset(look, params);
        }

        Matrix camera = multiply(rotation_yxz(new_base), rotation_yxz(st_.cam_rotation_offset));
        camera.t[0] = st_.cam_pos.x >> 4;
        camera.t[1] = st_.cam_pos.y >> 4;
        camera.t[2] = st_.cam_pos.z >> 4;
        st_.cam_matrix = camera;
        st_.cam_rotation = matrix_angles(camera);
    }

    // --- view output ---

    ViewOutput view_output(std::int32_t move_type) {
        ViewOutput out;
        if (st_.update_look_at_point) {
            st_.update_look_at_point = false;
            out.kind = ViewOutput::Kind::HeadLookAt;
            return out;
        }
        out.kind = ViewOutput::Kind::DirectMatrix;
        if (st_.update_look_at_matrix) {
            st_.update_look_at_matrix = false;
            out.matrix = st_.look_at_matrix;
            return out;
        }
        if (!is_move(move_type, CameraMove::SelfView)) {
            out.matrix = st_.cam_matrix;
            return out;
        }

        st_.self_view_timer += dt();
        const std::int32_t t = st_.self_view_timer;
        const Angles bob_angles{s16(head_bob(4, 5688, 9102, t)), s16(head_bob(2, 4551, 11377, t)), 0};
        Matrix bob = rotation_yxz(bob_angles);
        bob.m[0][0] = s16(bob.m[0][0] + head_bob(12, 7964, 10240, t));
        bob.m[0][1] = s16(bob.m[0][1] + head_bob(12, 6826, 11377, t));
        bob.m[0][2] = s16(bob.m[0][2] + head_bob(12, 6826, 9102, t));
        bob.m[1][0] = s16(bob.m[1][0] + head_bob(12, 5688, 5688, t));
        bob.m[1][1] = s16(bob.m[1][1] + head_bob(12, 10240, 4551, t));
        bob.m[1][2] = s16(bob.m[1][2] + head_bob(12, 7395, 10808, t));
        out.matrix = multiply(st_.cam_matrix, bob);
        out.matrix.t[0] = st_.cam_matrix.t[0];
        out.matrix.t[1] = st_.cam_matrix.t[1];
        out.matrix.t[2] = st_.cam_matrix.t[2];
        return out;
    }

    // 1 when the camera moved and turned little this frame (scaled to a 60 fps frame), else 0.
    std::int32_t smooth_move(const Vec3& start_pos, const Angles& start_rot) const {
        std::int32_t frames = fx::wrap_mul(dt(), 4096) / q12(1.0f / 60.0f);
        frames = clamp(frames, q12(1.0f), q12(4.0f));

        std::int32_t moved = fx::length((st_.cam_pos.x - start_pos.x) >> 4, (st_.cam_pos.y - start_pos.y) >> 4,
                                        (st_.cam_pos.z - start_pos.z) >> 4);
        moved = fx::wrap_mul(moved, 4096) / frames;
        if (moved > q12(0.0125f)) {
            return 0;
        }
        const Angles& now = st_.cam_rotation;
        const std::int32_t pitch_change = now.x - start_rot.x >= 0 ? now.x - start_rot.x : start_rot.x - now.x;
        if (fx::wrap_mul(pitch_change, 4096) / frames > angle(20.0f)) {
            return 0;
        }
        std::int32_t yaw_change = fx::wrap_mul(abs32(wrap_angle(now.y - start_rot.y)), 4096) / frames;
        yaw_change = mul(yaw_change, fx::cos(now.x));
        return yaw_change <= angle(30.0f) ? 1 : 0;
    }

    CameraState& st_;
    const FrameInput& in_;
    const CameraRig& rig_;
};

}  // namespace

StageCResult CameraRig::update_view(const FrameInput& input, const StageAResult& stage_a, const StageBResult& stage_b,
                                    const Vec3& frame_start_pos, const Angles& frame_start_rotation) {
    return ViewStep(state_, input, *this).run(stage_a, stage_b, frame_start_pos, frame_start_rotation);
}

FrameResult CameraRig::update(const FrameInput& input) {
    const Vec3 start_pos = state_.cam_pos;
    const Angles start_rot = state_.cam_rotation;
    FrameResult result;
    result.zone = update_zone(input);
    result.target = update_target(input, result.zone);
    result.view = update_view(input, result.zone, result.target, start_pos, start_rot);
    return result;
}

}  // namespace sh::camera
