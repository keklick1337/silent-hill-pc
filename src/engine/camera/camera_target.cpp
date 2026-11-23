// Stage B of the camera update (see camera_rig.h): the motion parameters of the frame, the camera
// target (where the camera wants to be) and the camera position following it. The arithmetic is
// the shipped game's, reproduced exactly (fixed point, truncation, 32-bit wraparound, tie rules).

#include <climits>

#include "engine/camera/camera_math.h"
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
using fx::q8;

// The camera never settles closer than this to the edge of its road box.
constexpr std::int32_t kRoadMargin = q12(1.0f);

// Radius of the zone around the character the follow camera is pushed out of, by the angle between
// the character's facing and the camera (0 = in front .. 180 degrees = behind).
constexpr std::int32_t kExclusionRadii[] = {q12(4.0f), q12(4.0f), q12(3.7f), q12(3.2f), q12(2.7f),
                                            q12(2.3f), q12(1.9f), q12(1.5f), q12(1.3f)};

// The 30 fps frame the shipped turn rate was tuned for.
constexpr std::int32_t kFrame30 = q12(1.0f / 30.0f);

bool is_move(std::int32_t move_type, CameraMove move) { return move_type == static_cast<std::int32_t>(move); }

// Keeps (x, z) at least kRoadMargin inside the box; a box narrower than twice the margin pins the
// coordinate to its centre.
void keep_inside_road(std::int32_t& x, std::int32_t& z, const Area& area) {
    std::int32_t min_x = from_q4(area.min_x) + kRoadMargin;
    std::int32_t max_x = from_q4(area.max_x) - kRoadMargin;
    std::int32_t min_z = from_q4(area.min_z) + kRoadMargin;
    std::int32_t max_z = from_q4(area.max_z) - kRoadMargin;
    if (max_x < min_x) {
        min_x = (min_x + max_x) >> 1;
        max_x = min_x;
    }
    if (max_z < min_z) {
        min_z = (min_z + max_z) >> 1;
        max_z = min_z;
    }
    x = clamp(x, min_x, max_x);
    z = clamp(z, min_z, max_z);
}

class TargetStep {
public:
    TargetStep(const CameraRig& rig, CameraState& st, const FrameInput& in) : rig_(rig), st_(st), in_(in) {}

    StageBResult run(const StageAResult& a) {
        StageBResult result;
        choose_params(a.self_view_rate, result);
        if ((st_.flags & camera_flags::user_camera) == 0) {
            update_target(a.move_type, result.cam_params);
        }
        follow_target(result.cam_params);
        return result;
    }

private:
    const CameraRoad& current_road() const { return rig_.road(st_.current_road.road); }
    AreaSize area_size() const { return current_road().area_size; }
    std::int32_t dt() const { return in_.delta_time; }

    void choose_params(std::int32_t self_view_rate, StageBResult& result) {
        if ((st_.flags & camera_flags::user_watch) != 0) {
            result.watch_params = st_.user_watch_params;
        } else {
            WatchMoveParams& p = st_.blended_watch_params;
            const WatchMoveParams& d = in_.tuning.follow_watch;
            const WatchMoveParams& s = in_.tuning.self_view_watch;
            p.accel_x = mul_wide(s.accel_x - d.accel_x, self_view_rate) + d.accel_x;
            p.accel_y = mul_wide(s.accel_y - d.accel_y, self_view_rate) + d.accel_y;
            p.max_speed_x = static_cast<std::int16_t>(d.max_speed_x + mul_wide(s.max_speed_x - d.max_speed_x, self_view_rate));
            p.max_speed_y = static_cast<std::int16_t>(d.max_speed_y + mul_wide(s.max_speed_y - d.max_speed_y, self_view_rate));
            // Turning the look-at faster while the character runs.
            const std::int32_t run_boost = clamp(mul_wide(st_.chara_move_speed, angle(360.0f)), 0, angle(720.0f));
            p.accel_y += run_boost;
            result.watch_params = p;
        }
        result.cam_params = (st_.flags & camera_flags::user_camera) != 0 ? st_.user_cam_params : in_.tuning.follow_camera;
    }

    // --- ideal camera positions per move type ---

    Vec3 ideal_self_view() const {
        if ((st_.flags & camera_flags::warp_watch) != 0) {
            return Vec3{st_.chara_pos.x, st_.chara_top_y, st_.chara_pos.z};
        }
        std::int32_t yaw;
        Vec3 ideal;
        if (in_.extra_view_mode) {
            yaw = st_.chara_eye_angle_y + angle(140.0f);
            ideal.y = st_.chara_head_pos.y + q12(0.07f);
        } else {
            yaw = st_.chara_eye_angle_y + angle(170.0f);
            ideal.y = st_.chara_head_pos.y + q12(0.1f);
        }
        // The over-the-shoulder distance was tuned as the angle constant 64.8 degrees (737).
        ideal.x = st_.chara_head_pos.x + mul(fx::sin(yaw), angle(64.8f));
        ideal.z = st_.chara_head_pos.z + mul(fx::cos(yaw), angle(64.8f));
        return ideal;
    }

    Vec3 ideal_fixed_angle() const {
        const CameraRoad& road = current_road();
        const std::int16_t shot_yaw = static_cast<std::int16_t>(road.fix_angle_y << 4);

        std::int32_t to_box_x = 0;
        std::int32_t to_box_z = 0;
        const Area& box = st_.current_road.road_area;
        area_distance(&to_box_x, &to_box_z, st_.chara_pos.x, st_.chara_pos.z, from_q4(box.min_x), from_q4(box.max_x),
                      from_q4(box.min_z), from_q4(box.max_z), false);
        const std::int32_t outside = abs32(to_box_z) < abs32(to_box_x) ? abs32(to_box_x) : abs32(to_box_z);
        const std::int32_t back_off = (outside >> 1) + q12(1.5f);

        const std::int32_t to_camera = fx::length(st_.chara_pos.x - st_.cam_pos.x, 0, st_.chara_pos.z - st_.cam_pos.z);
        std::int32_t lead;
        if (to_camera >= q12(7.0f)) {
            lead = 0;
        } else if (to_camera > q12(1.5f)) {
            lead = mul(fx::wrap_mul(to_camera - q12(7.0f), 4096) / q12(-5.5f), q12(0.7f));
        } else {
            lead = q12(0.7f);
        }

        Vec3 ideal;
        ideal.x = st_.chara_pos.x + mul(lead, fx::sin(st_.chara_eye_angle_y)) +
                  mul(back_off, fx::sin(shot_yaw + angle(180.0f)));
        ideal.z = st_.chara_pos.z + mul(lead, fx::cos(st_.chara_eye_angle_y)) +
                  mul(back_off, fx::cos(shot_yaw + angle(180.0f)));
        ideal.y = st_.chara_pos.y;
        // The zone's own road box, without the per-frame anchor shifts of the near list.
        keep_inside_road(ideal.x, ideal.z, road.road_area);
        return ideal;
    }

    Vec3 ideal_through_door(const Vec3& unchanged) const {
        const ThroughDoorRail& rail = st_.through_door;
        if (!rail.active) {
            return unchanged;
        }
        std::int32_t forward;
        std::int32_t lateral;
        if (st_.through_door_activate) {
            forward = q12(-1.3f);
            lateral = 0;
        } else {
            std::int32_t threshold;
            std::int32_t scale;
            if (in_.tight_doorways) {
                threshold = angle(15.0f);
                forward = q12(0.85f);
                lateral = q12(0.6f);
                scale = q12(0.7f);
            } else {
                threshold = angle(45.0f);
                forward = q12(0.75f);
                lateral = q12(0.7f);
                scale = q12(0.6f);
            }
            std::int16_t turned = static_cast<std::int16_t>(fx::wrap_angle(st_.chara_eye_angle_y - rail.rail_yaw));
            if (turned < 0) {
                turned = static_cast<std::int16_t>(-turned);
            }
            const std::int16_t past = static_cast<std::int16_t>((turned - threshold) < 0 ? 0 : turned - threshold);
            // Maps the turn past the threshold onto a half cosine period.
            const std::int32_t phase = (fx::wrap_mul(past * (q12(2048.0f) / (angle(180.0f) - threshold)), 16)) >> 16;
            forward = forward + (fx::wrap_mul(-lateral, fx::cos(phase)) >> 12);
            lateral = mul(-scale, fx::sin(st_.chara_eye_angle_y - rail.rail_yaw));
        }
        Vec3 ideal;
        ideal.x = rail.start.x + mul(forward, fx::sin(rail.rail_yaw)) + mul(lateral, fx::cos(rail.rail_yaw));
        ideal.z = rail.start.z + mul(forward, fx::cos(rail.rail_yaw)) + mul(lateral, -fx::sin(rail.rail_yaw));
        ideal.y = rail.start.y;
        return ideal;
    }

    Vec3 ideal_follow() {
        const std::int16_t behind = static_cast<std::int16_t>(st_.chara_eye_angle_y + angle(180.0f));
        std::int16_t swing = static_cast<std::int16_t>(fx::wrap_angle(st_.cam_to_ideal_angle_y - behind));

        // The shipped 12 degrees per 30 fps frame, scaled by the frame time (360 degrees a second).
        std::int16_t step = static_cast<std::int16_t>(fx::wrap_mul(angle(12.0f), dt()) / kFrame30);
        if (step <= 0) {
            step = 1;
        }
        if (abs32(st_.chara_angle_speed_y) > angle(20.0f)) {
            swing = static_cast<std::int16_t>(clamp(swing, -step, step));
        } else if (swing >= 0) {
            swing = step;
        } else {
            swing = static_cast<std::int16_t>(-step);
        }
        st_.cam_to_ideal_angle_y = static_cast<std::int16_t>(fx::wrap_angle(swing + behind));

        std::int32_t distance;
        switch (area_size()) {
            case AreaSize::Tiny:
                distance = q12(1.8f);
                break;
            case AreaSize::Small:
                distance = q12(2.0f);
                break;
            default:
                distance = q12(2.5f);
                break;
        }

        const CameraRoad& road = current_road();
        Vec3 ideal;
        ideal.x = st_.chara_pos.x + mul(distance, fx::sin(st_.cam_to_ideal_angle_y));
        ideal.y = st_.chara_top_y - q12(0.4f);
        ideal.z = st_.chara_pos.z + mul(distance, fx::cos(st_.cam_to_ideal_angle_y));

        std::int32_t height_gap = st_.cam_pos.y - st_.chara_pos.y;
        if (height_gap < 0) {
            height_gap = st_.chara_pos.y - st_.cam_pos.y;
        }
        height_gap -= q12(1.5f);
        if (height_gap < 0) {
            height_gap = 0;
        }

        ideal.y = clamp(ideal.y, from_q4(road.road_min_y), from_q4(road.road_max_y));

        std::int32_t inside_x = st_.chara_pos.x;
        std::int32_t inside_z = st_.chara_pos.z;
        keep_inside_road(inside_x, inside_z, st_.current_road.road_area);
        const std::int32_t off_road =
            fx::length(inside_x - st_.chara_pos.x, height_gap, inside_z - st_.chara_pos.z) >> 4;  // Q8

        std::int32_t blend_near;
        std::int32_t blend_far;
        if (area_size() == AreaSize::Tiny) {
            blend_near = q8(0.7f);
            blend_far = q8(2.0f);
        } else if (static_cast<int>(area_size()) < static_cast<int>(AreaSize::Wide)) {
            blend_near = q8(0.7f);
            blend_far = q8(3.0f);
        } else {
            blend_near = q8(2.0f);
            blend_far = q8(5.0f);
        }

        // Away from the road the camera pulls closer. The far value is a Q8 constant mixed into Q12
        // distances; that is the shipped tuning.
        std::int32_t final_distance;
        if (blend_far >= off_road) {
            if (blend_near < off_road) {
                final_distance =
                    distance + fx::wrap_mul(q8(6.4f) - distance, off_road - blend_near) / (blend_far - blend_near);
            } else {
                final_distance = distance;
            }
        } else {
            final_distance = q12(0.4f);
        }

        ideal.x = st_.chara_pos.x + mul_wide(final_distance, fx::sin(st_.cam_to_ideal_angle_y));
        ideal.z = st_.chara_pos.z + mul_wide(final_distance, fx::cos(st_.cam_to_ideal_angle_y));
        keep_inside_road(ideal.x, ideal.z, st_.current_road.road_area);
        return ideal;
    }

    // --- the target move of the frame ---

    std::int32_t max_target_step(const CamMoveParams& params) const {
        std::int32_t speed = (st_.chara_move_speed + q12(1.0f)) + abs32(st_.chara_angle_speed_y * 8);
        speed = speed < q12(2.2f) ? q12(2.2f) : speed;
        speed = params.max_speed_xz > speed ? speed : params.max_speed_xz;
        return mul_wide(speed, dt());
    }

    Vec3 step_toward(const Vec3& ideal, std::int32_t max_step) const {
        const std::int32_t dx = ideal.x - st_.cam_target_pos.x;
        const std::int32_t dz = ideal.z - st_.cam_target_pos.z;
        const std::int32_t distance = fx::length(dx, 0, dz);
        const std::int32_t heading = fx::atan2(dx, dz);
        Vec3 step;
        if (distance < max_step) {
            step.x = ideal.x - st_.cam_target_pos.x;
            step.z = ideal.z - st_.cam_target_pos.z;
        } else {
            step.x = mul(max_step, fx::sin(heading));
            step.z = mul(max_step, fx::cos(heading));
        }
        if (dt() == 0 && (st_.flags & camera_flags::warp_camera_target) == 0) {
            step.y = 0;
        } else {
            step.y = ideal.y - st_.cam_target_pos.y;
        }
        return step;
    }

    // Keeps the target height inside the band the zone's vertical rule allows.
    void limit_height(Vec3& step) const {
        const CameraRoad& road = current_road();
        const std::int32_t to_chara =
            fx::length(st_.chara_pos.x - st_.cam_target_pos.x, 0, st_.chara_pos.z - st_.cam_target_pos.z);
        const std::int32_t near = clamp(to_chara, q12(1.2f), q12(7.0f));
        const std::int32_t closeness = clamp(fx::mul_by(q12(7.0f) - near, 0.1724f), 0, q12(1.0f));

        std::int32_t low;
        std::int32_t high;
        switch (road.move_y_type) {
            case static_cast<std::uint8_t>(CameraMove::Settle):
                low = mul_wide(from_q4(road.road_max_y), q12(1.0f) - closeness) +
                      mul_wide(from_q4(road.road_min_y), closeness);
                high = low;
                break;
            case static_cast<std::uint8_t>(CameraMove::FixedAngle):
                low = mul_wide(from_q4(road.road_min_y), q12(1.0f) - closeness) +
                      mul_wide(from_q4(road.road_max_y), closeness);
                high = low;
                break;
            case static_cast<std::uint8_t>(CameraMove::SelfView):
                low = from_q4(road.road_min_y);
                high = from_q4(road.road_max_y);
                break;
            default: {
                std::int32_t spread = (to_chara - q12(0.3f)) >> 2;
                if (spread < 0) {
                    spread = 0;
                }
                high = (spread + st_.chara_top_y) - q12(0.25f);
                low = (st_.chara_top_y - spread) - q12(0.25f);
                break;
            }
        }
        const std::int32_t y = clamp(st_.cam_target_pos.y + step.y, low, high);
        step.y = y - st_.cam_target_pos.y;
    }

    // Nearest near-list entry of one zone kind by its road box, among merge zones only.
    const NearRoad* nearest_merge_road(RoadType kind, std::int32_t x, std::int32_t z, std::int32_t* out_distance) const {
        const NearRoad* best = nullptr;
        std::int32_t best_distance = INT_MAX;
        for (int i = 0; i < st_.near_road_count; ++i) {
            const NearRoad& near = st_.near_roads[i];
            const CameraRoad& road = rig_.road(near.road);
            if (road.road_type != kind || (road.flags & road_flags::merge_road) == 0) {
                continue;
            }
            std::int32_t unused_x;
            std::int32_t unused_z;
            const std::int32_t distance =
                area_distance(&unused_x, &unused_z, x, z, from_q4(near.road_area.min_x), from_q4(near.road_area.max_x),
                              from_q4(near.road_area.min_z), from_q4(near.road_area.max_z), true);
            if (best_distance >= distance) {
                best_distance = distance;
                best = &near;
            }
        }
        *out_distance = best_distance;
        return best;
    }

    // How far a target position must be pushed out of the zone the follow camera keeps clear around
    // the character; also returns the push direction. The zone radius eases in over frames.
    std::int32_t exclusion_push(std::int32_t* push_yaw, std::int32_t x, std::int32_t z) {
        const std::int16_t yaw_from_chara = static_cast<std::int16_t>(fx::atan2(x - st_.chara_pos.x, z - st_.chara_pos.z));
        std::int32_t off_facing = fx::wrap_angle(yaw_from_chara - st_.chara_eye_angle_y);
        const std::int16_t off_facing_abs = static_cast<std::int16_t>(off_facing < 0 ? -off_facing : off_facing);

        std::int32_t base;
        switch (area_size()) {
            case AreaSize::Small:
            case AreaSize::Wide:
                base = q12(1.0f);
                break;
            default:
                base = q12(0.7f);
                break;
        }
        std::int32_t radius =
            fx::wrap_mul(base, piecewise_linear(kExclusionRadii, 9, off_facing_abs, 0, angle(180.0f))) >> 12;

        if (st_.old_cam_exclusion_radius != -1) {
            std::int32_t change = radius - st_.old_cam_exclusion_radius;
            const std::int32_t shrink_limit = mul_wide(dt(), angle(-180.0f));
            if (change < shrink_limit) {
                change = shrink_limit;
            }
            radius = st_.old_cam_exclusion_radius + change;
        }
        st_.old_cam_exclusion_radius = radius;

        const std::int32_t distance = fx::length(x - st_.chara_pos.x, 0, z - st_.chara_pos.z);
        *push_yaw = yaw_from_chara;
        return distance < radius ? radius - distance : 0;
    }

    void push_out_of_exclusion(Vec3& step, std::int32_t max_step) {
        const std::int32_t pre_x = step.x + st_.cam_target_pos.x;
        const std::int32_t pre_z = step.z + st_.cam_target_pos.z;
        std::int32_t push_yaw = 0;
        const std::int32_t push = exclusion_push(&push_yaw, pre_x, pre_z);
        if (push <= 0) {
            return;
        }

        const NearRoad* bounds = &st_.current_road;
        NearRoad fallback;
        if ((current_road().flags & road_flags::merge_road) != 0) {
            std::int32_t merge_distance = 0;
            const NearRoad* nearest = nearest_merge_road(current_road().road_type, pre_x, pre_z, &merge_distance);
            if (nearest == nullptr) {
                const CameraRoad def = default_camera_road();
                fallback.road_area = def.road_area;
                bounds = &fallback;
            } else if (merge_distance > 0) {
                bounds = &st_.current_road;
            } else {
                bounds = nearest;
            }
        }

        std::int32_t post_x = pre_x + mul(push, fx::sin(push_yaw));
        std::int32_t post_z = pre_z + mul(push, fx::cos(push_yaw));
        keep_inside_road(post_x, post_z, bounds->road_area);

        step.x = post_x - st_.cam_target_pos.x;
        step.z = post_z - st_.cam_target_pos.z;
        if (max_step < fx::length(step.x, 0, step.z)) {
            const std::int32_t heading = fx::atan2(step.x, step.z);
            step.x = mul_wide(max_step, fx::sin(heading));
            step.z = mul_wide(max_step, fx::cos(heading));
        }
    }

    void update_target(std::int32_t move_type, const CamMoveParams& params) {
        Vec3 ideal;
        if (is_move(move_type, CameraMove::SelfView)) {
            ideal = ideal_self_view();
        } else if (is_move(move_type, CameraMove::FixedAngle)) {
            ideal = ideal_fixed_angle();
        } else if (is_move(move_type, CameraMove::ThroughDoor)) {
            ideal = ideal_through_door(st_.cam_target_pos);
        } else {
            ideal = ideal_follow();
        }

        if ((st_.flags & camera_flags::warp_camera_target) != 0) {
            st_.cam_target_pos = ideal;
        }

        Vec3 step;
        if (!is_move(move_type, CameraMove::SelfView)) {
            const std::int32_t max_step = max_target_step(params);
            step = step_toward(ideal, max_step);
            if (is_move(move_type, CameraMove::Chase) && (current_road().flags & road_flags::no_front_flip) == 0) {
                push_out_of_exclusion(step, max_step);
            }
            if (!is_move(move_type, CameraMove::ThroughDoor)) {
                limit_height(step);
            }
        } else {
            step = step_toward(ideal, q12(1.0f));
        }

        st_.cam_target_move_angle_y = static_cast<std::int16_t>(fx::atan2(step.x, step.z));

        if (dt() != 0 || (st_.flags & camera_flags::warp_camera_target) != 0) {
            st_.cam_target_pos.x += step.x;
            st_.cam_target_pos.y += step.y;
            st_.cam_target_pos.z += step.z;
            if (dt() == 0) {
                // A warp on a frame without elapsed time: no velocity instead of a division by zero.
                st_.cam_target_velocity = Vec3{};
                st_.cam_target_speed = 0;
                return;
            }
            st_.cam_target_velocity.x = fx::wrap_mul(step.x, 4096) / dt();
            st_.cam_target_velocity.y = fx::wrap_mul(step.y, 4096) / dt();
            st_.cam_target_velocity.z = fx::wrap_mul(step.z, 4096) / dt();
            st_.cam_target_speed = fx::length(st_.cam_target_velocity.x, 0, st_.cam_target_velocity.z);
            return;
        }
        st_.cam_target_velocity.x = 0;
        st_.cam_target_velocity.z = 0;
        st_.cam_target_speed = 0;
    }

    // --- the camera follows its target ---

    void follow_target(const CamMoveParams& params) {
        if ((st_.flags & camera_flags::warp_camera) != 0) {
            st_.cam_move_angle_y = static_cast<std::int16_t>(
                fx::atan2(st_.cam_target_pos.x - st_.cam_pos.x, st_.cam_target_pos.z - st_.cam_pos.z));
            st_.cam_pos = st_.cam_target_pos;
            st_.cam_velocity = Vec3{};
            return;
        }

        const std::int32_t brake_xz = fx::mul_by(params.accel_xz, 0.4f);
        const std::int32_t brake_y = fx::mul_by(params.accel_y, 1.0f);

        steer_planar_velocity(st_.cam_velocity.x, st_.cam_velocity.z, st_.cam_pos, st_.cam_target_pos, q12(0.1f),
                              params.accel_xz, params.max_speed_xz, brake_xz, q12(12.0f), dt());
        st_.cam_velocity.y = speed_toward(st_.cam_velocity.y, st_.cam_pos.y, st_.cam_target_pos.y, params.accel_y,
                                          params.max_speed_y, brake_y, dt());
        st_.cam_move_angle_y = static_cast<std::int16_t>(fx::atan2(st_.cam_velocity.x, st_.cam_velocity.z));

        st_.cam_pos.x += mul_wide(st_.cam_velocity.x, dt());
        st_.cam_pos.y += mul_wide(st_.cam_velocity.y, dt());
        st_.cam_pos.z += mul_wide(st_.cam_velocity.z, dt());
    }

    const CameraRig& rig_;
    CameraState& st_;
    const FrameInput& in_;
};

}  // namespace

StageBResult CameraRig::update_target(const FrameInput& input, const StageAResult& stage_a) {
    return TargetStep(*this, state_, input).run(stage_a);
}

}  // namespace sh::camera
