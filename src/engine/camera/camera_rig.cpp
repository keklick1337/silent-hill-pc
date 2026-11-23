// Stage A of the camera update (see camera_rig.h). The arithmetic is the original view camera's,
// reproduced exactly: every constant, comparison direction and division order below is part of
// the bit-exactness contract and marked where it is easy to get wrong.

#include "engine/camera/camera_rig.h"

#include <climits>

#include "engine/world/fixed_trig.h"

namespace sh::camera {

namespace {

namespace world = ::sh::world;

// Float-to-fixed constants, evaluated the way the game's macros did (multiply in floating point,
// truncate toward zero) so the values below match the shipped bits. Constant evaluation follows
// the same IEEE rules, so these are the game's numbers, not near-misses.
constexpr std::int32_t q12(float value) { return static_cast<std::int32_t>(value * 4096.0f); }
constexpr std::int32_t q8(double value) { return static_cast<std::int32_t>(value * 256.0); }
constexpr std::int32_t angle_q12(float degrees) { return q12(degrees / 360.0f); }

// The game's angle normalisation: sign-extend the low 12 bits. Written without shifts so there
// is no reliance on arithmetic-shift semantics.
constexpr std::int32_t normalize_angle(std::int32_t angle) { return ((angle & 0xFFF) ^ 0x800) - 0x800; }

// --- tuned constants of the original (values in parentheses) ---

constexpr std::int32_t kDeadTimeMax        = q12(10.0f);    // 40960: death timer clamp, seconds
constexpr std::int32_t kEnemyDeadTimeMax   = q12(1.5f);     // 6144: enemy counts this long after death
constexpr std::int32_t kEnemyDistanceMax   = q12(15.0f);    // 61440: enemy search reach, Q12
constexpr std::int32_t kGatherHalfWidth    = q12(20.0f);    // 81920: near-zone gathering half width
constexpr std::int32_t kKeepAdvantageRoad  = q12(0.35f);    // 1433: hysteresis margin, road/effect zones
constexpr std::int32_t kKeepAdvantageEvent = q12(0.15f);    // 614: hysteresis margin, event zones
constexpr std::int32_t kRailStartDropY     = q12(1.95f);    // 7987: rail start below the character's feet
constexpr std::int32_t kDoorEndTimer       = q12(1.2f);     // 4915: rail ends after this long...
constexpr std::int32_t kDoorEndNearEnemy   = q12(1.2f);     // 4915: ...when an enemy is this close
constexpr std::int32_t kDoorEndDistance    = q12(2.3f);     // 9420: rail ends past this reach
constexpr std::int32_t kDoorAngleDistance  = q12(0.5f);     // 2048: past this reach the heading also ends it
constexpr std::int32_t kDoorEndAngle       = angle_q12(70.0f);   // 796
constexpr std::int32_t kFarLookFull        = q12(0.9f);     // 3686: far-look rate at the rail start
constexpr std::int32_t kFarLookAngleScale  = q12(0.1945f);  // 796 (the same 70 degrees, as a rate)
constexpr std::int32_t kSelfViewRateMax    = q8(16.0);      // 4096: self-view blend ceiling
constexpr std::int32_t kDoorViewRateMax    = q8(5.6);       // 1433: rail-camera blend ceiling
constexpr std::int32_t kSelfViewNearHead   = q8(0.5);       // 128: full blend inside this head distance
constexpr std::int32_t kSelfViewFarHead    = q8(1.2);       // 307: no blend beyond this head distance
constexpr std::int32_t kSelfViewRangeDiv   = q8(0.7);       // 179: head-distance falloff divisor
constexpr std::int32_t kSelfViewEnemyFar   = q12(4.0f);     // 16384: full blend with an enemy this far
constexpr std::int32_t kSelfViewEnemyNear  = q12(2.0f);     // 8192: no blend with an enemy this close

constexpr std::int32_t kNoRadius = -1;  // old_cam_exclusion_radius "unset" marker

constexpr std::int32_t angle_zero    = 0;
constexpr std::int32_t angle_quarter = angle_q12(90.0f);   // 1024
constexpr std::int32_t angle_half    = angle_q12(180.0f);  // 2048

std::uint8_t zone_priority(RoadType type) {
    switch (type) {
        case RoadType::Event:
            return 2;
        case RoadType::Effect:
            return 1;
        case RoadType::Road:
        default:
            return 0;
    }
}

std::int32_t keep_advantage(RoadType type) {
    switch (type) {
        case RoadType::Event:
            return kKeepAdvantageEvent;
        case RoadType::Road:
        case RoadType::Effect:
        default:
            return kKeepAdvantageRoad;
    }
}

// --- nearest enemy ---

void update_nearest_enemy(CameraState& st, const FrameInput& in, const std::vector<std::int32_t>& dead_times) {
    std::int32_t all_min = kEnemyDistanceMax;
    std::int32_t active_min = kEnemyDistanceMax;
    std::size_t all_slot = static_cast<std::size_t>(-1);
    std::size_t active_slot = static_cast<std::size_t>(-1);

    if (in.combat_suppresses_enemies) {
        st.nearest_enemy = -1;
        st.nearest_enemy_xz_distance = kEnemyDistanceMax;
        return;
    }

    for (std::size_t i = 0; i < in.npcs.size(); ++i) {
        const NpcView& npc = in.npcs[i];
        if (!npc.camera_enemy) {
            continue;
        }
        // Freshly dead enemies still count for a moment (the timers were updated first).
        if (!(dead_times[i] <= kEnemyDeadTimeMax || npc.health >= 0)) {
            continue;
        }
        if (npc.excluded) {
            continue;
        }

        const std::int32_t offset_x = npc.position.x - st.chara_pos.x;
        const std::int32_t offset_z = npc.position.z - st.chara_pos.z;
        // A 15 m box, rejecting AT the bound (>=), then the true distance.
        if (offset_x >= kEnemyDistanceMax || offset_x <= -kEnemyDistanceMax ||
            offset_z >= kEnemyDistanceMax || offset_z <= -kEnemyDistanceMax) {
            continue;
        }
        const std::int32_t xz_distance = world::fixed_length3(offset_x, 0, offset_z);

        if (xz_distance < all_min) {
            all_min = xz_distance;
            all_slot = i;
        }

        // "Active": always-counting kinds are always active; the rest are active unless passive,
        // and a passive one stays active while it is the player's aimed-at target.
        const bool active =
            npc.always_counts || (!npc.passive || (npc.is_target && in.player_aiming));
        if (active && xz_distance < active_min) {
            active_min = xz_distance;
            active_slot = i;
        }
    }

    if (active_slot != static_cast<std::size_t>(-1)) {
        st.nearest_enemy = static_cast<std::int32_t>(active_slot);
        st.nearest_enemy_xz_distance = active_min;
    } else {
        st.nearest_enemy = all_slot != static_cast<std::size_t>(-1) ? static_cast<std::int32_t>(all_slot) : -1;
        st.nearest_enemy_xz_distance = all_min;
    }
}

// --- near-zone gathering ---

void gather_near_roads(const CameraRig& rig, CameraState& st, const FrameInput& in) {
    st.near_road_count = 0;

    const std::int32_t low_x = st.chara_pos.x - kGatherHalfWidth;
    const std::int32_t high_x = st.chara_pos.x + kGatherHalfWidth;
    const std::int32_t low_z = st.chara_pos.z - kGatherHalfWidth;
    const std::int32_t high_z = st.chara_pos.z + kGatherHalfWidth;

    const bool enemy_near = st.nearest_enemy != -1;

    for (RoadRef index = 0; index < static_cast<RoadRef>(rig.road_count()); ++index) {
        const CameraRoad& rd = rig.road(index);
        if ((rd.flags & road_flags::end_marker) != 0) {
            break;
        }

        // Zones can demand an enemy's presence or absence; the wrong condition skips the zone.
        const std::uint8_t required = enemy_near ? road_flags::use_without_enemy : road_flags::use_with_enemy;
        if ((rd.flags & required) != 0) {
            continue;
        }

        std::int32_t rd_min_x = static_cast<std::int32_t>(rd.road_area.min_x) << 8;
        std::int32_t rd_max_x = static_cast<std::int32_t>(rd.road_area.max_x) << 8;
        const std::int32_t rd_min_z = static_cast<std::int32_t>(rd.road_area.min_z) << 8;
        const std::int32_t rd_max_z = static_cast<std::int32_t>(rd.road_area.max_z) << 8;

        // Optional anchor corrections (a shipped-camera fix the bridge resolves by map/room).
        for (const RoadAnchorShift& shift : in.road_anchor_shifts) {
            if (shift.road == index) {
                rd_min_x += shift.x_shift;
                rd_max_x += shift.x_shift;
            }
        }

        const std::int32_t sw_min_x = static_cast<std::int32_t>(rd.switch_area.min_x) << 8;
        const std::int32_t sw_max_x = static_cast<std::int32_t>(rd.switch_area.max_x) << 8;
        const std::int32_t sw_min_z = static_cast<std::int32_t>(rd.switch_area.min_z) << 8;
        const std::int32_t sw_max_z = static_cast<std::int32_t>(rd.switch_area.max_z) << 8;

        // The character's 20 m box must overlap the switch box or the road box.
        if ((high_x >= sw_min_x && sw_max_x >= low_x && high_z >= sw_min_z && sw_max_z >= low_z) ||
            (high_x >= rd_min_x && rd_max_x >= low_x && high_z >= rd_min_z && rd_max_z >= low_z)) {
            if (st.near_road_count >= kNearRoadCapacity) {
                // The original wrote past the end of its fixed list here; no shipped map gathers
                // that many zones (checked on all 43 maps), so stopping changes nothing.
                break;
            }
            NearRoad& entry = st.near_roads[st.near_road_count];
            entry.road = index;
            entry.axis = ((rd_max_z - rd_min_z) < (rd_max_x - rd_min_x)) ? 1 : 0;
            entry.distance = area_distance(&entry.offset_x, &entry.offset_z,
                                           st.chara_pos.x, st.chara_pos.z,
                                           sw_min_x, sw_max_x, sw_min_z, sw_max_z,
                                           (rd.flags & road_flags::merge_road) != 0);
            entry.priority = zone_priority(rd.road_type);
            entry.road_area = Area{static_cast<std::int16_t>(rd_min_x >> 8),
                                   static_cast<std::int16_t>(rd_max_x >> 8),
                                   static_cast<std::int16_t>(rd_min_z >> 8),
                                   static_cast<std::int16_t>(rd_max_z >> 8)};
            entry.switch_area = Area{static_cast<std::int16_t>(sw_min_x >> 8),
                                     static_cast<std::int16_t>(sw_max_x >> 8),
                                     static_cast<std::int16_t>(sw_min_z >> 8),
                                     static_cast<std::int16_t>(sw_max_z >> 8)};
            ++st.near_road_count;
        }
    }
}

// --- current-zone selection ---

// Nearest near-list entry of one zone kind, by the switch-box distance. Ties keep the LATER entry
// (the original's >=), and the distance runs through the merge rule of the entry's own zone.
int nearest_of_kind(const CameraRig& rig, const CameraState& st, RoadType kind, std::int32_t* out_distance) {
    int best = -1;
    std::int32_t min_distance = INT_MAX;
    for (int i = 0; i < st.near_road_count; ++i) {
        const NearRoad& near = st.near_roads[i];
        const CameraRoad& rd = rig.road(near.road);
        if (rd.road_type != kind) {
            continue;
        }
        std::int32_t unused_x;
        std::int32_t unused_z;
        const std::int32_t distance =
            area_distance(&unused_x, &unused_z, st.chara_pos.x, st.chara_pos.z,
                          static_cast<std::int32_t>(near.switch_area.min_x) << 8,
                          static_cast<std::int32_t>(near.switch_area.max_x) << 8,
                          static_cast<std::int32_t>(near.switch_area.min_z) << 8,
                          static_cast<std::int32_t>(near.switch_area.max_z) << 8,
                          (rd.flags & road_flags::merge_road) != 0);
        if (min_distance >= distance) {
            min_distance = distance;
            best = i;
        }
    }
    *out_distance = min_distance;
    return best;
}

// The original's three-round choice (event, then road, then effect), reproduced branch by branch:
// the chain looks redundant with its initial priorities but the eff round deliberately does not
// update the running priority, and later rounds can still displace an earlier pick by distance.
int best_new_road(const CameraRig& rig, const CameraState& st, std::int32_t* out_distance) {
    std::int32_t event_distance = 0;
    std::int32_t road_distance = 0;
    std::int32_t effect_distance = 0;
    const int event_index = nearest_of_kind(rig, st, RoadType::Event, &event_distance);
    const int road_index = nearest_of_kind(rig, st, RoadType::Road, &road_distance);
    const int effect_index = nearest_of_kind(rig, st, RoadType::Effect, &effect_distance);

    int chosen = -1;
    std::int32_t chosen_distance = INT_MAX;
    std::int32_t chosen_priority = 0;

    bool renew = false;
    if (event_index != -1) {
        const std::int32_t priority = st.near_roads[event_index].priority;
        if (chosen_priority < priority) {
            if (event_distance <= 0 || event_distance < chosen_distance) {
                renew = true;
            }
        } else if (priority >= chosen_priority) {
            if (event_distance < chosen_distance) {
                renew = true;
            }
        } else if (chosen_distance > 0 && event_distance < chosen_distance) {
            renew = true;
        }
    }
    if (renew) {
        chosen = event_index;
        chosen_distance = event_distance;
        chosen_priority = st.near_roads[chosen].priority;
    }

    renew = false;
    if (road_index != -1) {
        const std::int32_t priority = st.near_roads[road_index].priority;
        if (chosen_priority < priority) {
            if (road_distance <= 0 || road_distance < chosen_distance) {
                renew = true;
            }
        } else if (priority >= chosen_priority) {
            if (road_distance < chosen_distance) {
                renew = true;
            }
        } else if (chosen_distance > 0 && road_distance < chosen_distance) {
            renew = true;
        }
    }
    if (renew) {
        chosen = road_index;
        chosen_distance = road_distance;
        chosen_priority = st.near_roads[chosen].priority;
    }

    renew = false;
    if (effect_index != -1) {
        const std::int32_t priority = st.near_roads[effect_index].priority;
        if (chosen_priority < priority) {
            if (effect_distance <= 0 || effect_distance < chosen_distance) {
                renew = true;
            }
        } else if (priority >= chosen_priority) {
            if (effect_distance < chosen_distance) {
                renew = true;
            }
        } else if (chosen_distance > 0 && effect_distance < chosen_distance) {
            renew = true;
        }
    }
    if (renew) {
        chosen = effect_index;
        chosen_distance = effect_distance;
        // Deliberately no priority update in this round, as in the original.
    }

    if (chosen == -1) {
        // No zone of the map engaged: the fallback zone, distance from its own road box
        // (its merge rule is clear, so the distance never goes negative here).
        const CameraRoad def = default_camera_road();
        std::int32_t unused_x;
        std::int32_t unused_z;
        chosen_distance = area_distance(&unused_x, &unused_z, st.chara_pos.x, st.chara_pos.z,
                                        static_cast<std::int32_t>(def.road_area.min_x) << 8,
                                        static_cast<std::int32_t>(def.road_area.max_x) << 8,
                                        static_cast<std::int32_t>(def.road_area.min_z) << 8,
                                        static_cast<std::int32_t>(def.road_area.max_z) << 8,
                                        (def.flags & road_flags::merge_road) != 0);
    }

    *out_distance = chosen_distance;
    return chosen;
}

// The zone handover with hysteresis. Returns true when the camera must warp.
bool select_current_road(const CameraRig& rig, CameraState& st) {
    bool warp = false;

    std::int32_t new_distance = 0;
    const int new_index = best_new_road(rig, st, &new_distance);
    NearRoad new_current = new_index != -1 ? st.near_roads[new_index] : fallback_near_road();
    new_current.road = rig.resolve_road(new_current.road);

    // Is the current zone still in the near list? (The original compared road pointers; refs
    // resolved the same way carry the same equality, including the fallback-is-entry-0 case.)
    int old_index = -1;
    for (int i = 0; i < st.near_road_count; ++i) {
        if (rig.resolve_road(st.near_roads[i].road) == rig.resolve_road(st.current_road.road)) {
            old_index = i;
        }
    }

    const CameraRoad& new_road = rig.road(new_current.road);

    if (old_index == -1) {
        if ((new_road.flags & road_flags::warp_in) != 0) {
            warp = true;
        }
        if ((rig.road(st.current_road.road).flags & road_flags::warp_out) != 0) {
            warp = true;
        }
        st.current_road = new_current;
        return warp;
    }

    const NearRoad& old_current = st.near_roads[old_index];
    const std::int32_t advantage = keep_advantage(rig.road(old_current.road).road_type);

    if (new_current.priority < old_current.priority && old_current.distance < advantage * 2) {
        // A lower-priority zone does not steal the frame this close to the old one.
        st.current_road = old_current;
    } else {
        if (old_current.priority < new_current.priority && new_current.distance <= 0) {
            if ((new_road.flags & road_flags::warp_in) != 0) {
                warp = true;
            }
            if ((rig.road(st.current_road.road).flags & road_flags::warp_out) != 0) {
                warp = true;
            }
            st.current_road = new_current;
            return warp;
        }

        // How far the character is headed off the old zone's axis, folded to 0..90 degrees.
        const std::int32_t old_zone_yaw = old_current.axis == 0 ? angle_zero : angle_quarter;
        std::int32_t heading_offset = normalize_angle(st.chara_move_angle_y - old_zone_yaw);
        if (heading_offset < angle_zero) {
            heading_offset += angle_half;
        }
        if (heading_offset > angle_quarter) {
            heading_offset = angle_half - heading_offset;
        }

        const std::int32_t keep_frame = old_current.distance - advantage;
        if (new_distance >= keep_frame) {
            st.current_road = old_current;
        } else if (old_current.distance < 0 && heading_offset < angle_q12(20.0f)) {
            st.current_road = old_current;
        } else {
            if ((new_road.flags & road_flags::warp_in) != 0) {
                warp = true;
            }
            if ((rig.road(st.current_road.road).flags & road_flags::warp_out) != 0) {
                warp = true;
            }
            st.current_road = new_current;
        }
    }

    return warp;
}

// --- move type ---

bool through_door_ended(const CameraState& st) {
    const ThroughDoorRail& rail = st.through_door;
    const std::int32_t distance = rail.distance;

    if (!rail.active) {
        return true;
    }
    if (rail.timer > kDoorEndTimer && st.nearest_enemy_xz_distance < kDoorEndNearEnemy) {
        return true;
    }
    if (distance > kDoorEndDistance) {
        return true;
    }
    if (distance > kDoorAngleDistance) {
        std::int32_t heading = normalize_angle(
            st.chara_eye_angle_y -
            world::fixed_atan2(st.chara_pos.x - rail.start.x, st.chara_pos.z - rail.start.z));
        if (heading < angle_zero) {
            heading = -heading;
        }
        if (heading > kDoorEndAngle) {
            return true;
        }
    }
    return false;
}

std::int32_t current_move_type(const FrameInput& in, CameraState& st, const CameraRig& rig) {
    if (in.extra_view_mode) {
        const bool held = (st.flags & camera_flags::view_button) != 0;
        if ((in.extra_view_ctrl && !held) || (!in.extra_view_ctrl && held)) {
            if ((st.flags & (camera_flags::user_camera | camera_flags::user_watch |
                             camera_flags::inhibit_far_look)) == 0 &&
                !in.self_view_blocked) {
                return static_cast<std::int32_t>(CameraMove::SelfView);
            }
        }
    }

    if (st.through_door.active) {
        if (!through_door_ended(st)) {
            return static_cast<std::int32_t>(CameraMove::ThroughDoor);
        }
        st.through_door.active = false;
        st.through_door.timer = 0;
    }

    return static_cast<std::int32_t>(rig.road(st.current_road.road).camera_move);
}

// --- rates and flags ---

std::int32_t far_look_rate(const FrameInput& in, const CameraState& st, std::int32_t move_type) {
    std::int32_t rate = 0;

    if ((st.flags & (camera_flags::user_watch | camera_flags::inhibit_far_look)) != 0) {
        rate = 0;
    } else {
        switch (move_type) {
            case static_cast<std::int32_t>(CameraMove::Chase):
            case static_cast<std::int32_t>(CameraMove::Settle): {
                // The far-look button, as gated by the "reverse" option: pressed XOR reversed.
                const bool pressed = ((st.flags & camera_flags::view_button) != 0) != in.extra_view_ctrl;
                rate = pressed ? 4096 : 0;
                break;
            }

            case static_cast<std::int32_t>(CameraMove::ThroughDoor): {
                rate = 4096;
                const bool pressed = ((st.flags & camera_flags::view_button) != 0) != in.extra_view_ctrl;
                if (!pressed) {
                    const std::int32_t distance = st.through_door.distance;
                    rate = kFarLookFull - (distance * kFarLookFull) / kDoorEndDistance;
                    if (rate < 0) {
                        rate = 0;
                    }
                    if (distance > kDoorAngleDistance) {
                        const std::int32_t rail_x = st.chara_pos.x - st.through_door.start.x;
                        const std::int32_t rail_z = st.chara_pos.z - st.through_door.start.z;
                        const std::int32_t raw = st.chara_eye_angle_y - world::fixed_atan2(rail_x, rail_z);
                        // NOT an absolute value: the original tested bit 11 of the raw (unwrapped)
                        // difference and negated the NORMALIZED one only in that case, so inputs
                        // an odd multiple of a half-turn out produce a negative "offset" here.
                        const std::int32_t offset =
                            (static_cast<std::int32_t>(static_cast<std::uint32_t>(raw) << 20) < 0)
                                ? -normalize_angle(raw)
                                : normalize_angle(raw);
                        rate = (rate * (kFarLookAngleScale - offset)) / kFarLookAngleScale;
                        if (rate < 0) {
                            rate = 0;
                        }
                    }
                }
                break;
            }

            default:
                rate = 0;
                break;
        }
    }

    if (in.extra_view_mode) {
        const std::int32_t held = (st.flags >> 9) & 1;  // view_button bit
        if ((in.extra_view_ctrl && held == 0) || (!in.extra_view_ctrl && held != 0)) {
            if ((st.flags & (camera_flags::user_camera | camera_flags::user_watch |
                             camera_flags::inhibit_far_look)) == 0 &&
                in.self_view_blocked) {
                rate = 0;
            }
        }
    }

    return rate;
}

std::int32_t self_view_rate(const CameraState& st, std::int32_t move_type) {
    if (move_type > static_cast<std::int32_t>(CameraMove::ThroughDoor)) {
        return 0;
    }
    if (move_type < static_cast<std::int32_t>(CameraMove::SelfView)) {
        return 0;
    }

    const std::int32_t ceiling =
        move_type == static_cast<std::int32_t>(CameraMove::SelfView) ? kSelfViewRateMax : kDoorViewRateMax;

    // Head distance in Q8: the components are shifted down BEFORE the magnitude.
    const std::int32_t head_distance =
        world::fixed_length3((st.cam_pos.x - st.chara_head_pos.x) >> 4,
                             (st.cam_pos.y - st.chara_head_pos.y) >> 4,
                             (st.cam_pos.z - st.chara_head_pos.z) >> 4);

    std::int32_t max_rate = 0;
    if (head_distance >= kSelfViewNearHead) {
        if (head_distance > kSelfViewFarHead) {
            max_rate = 0;
        } else {
            max_rate = (ceiling * (kSelfViewFarHead - head_distance)) / kSelfViewRangeDiv;
        }
    } else {
        max_rate = ceiling;
    }

    std::int32_t near_scale = 0;
    if (st.nearest_enemy_xz_distance > kSelfViewEnemyFar) {
        near_scale = 4096;
    } else if (st.nearest_enemy_xz_distance < kSelfViewEnemyNear) {
        near_scale = 0;
    } else {
        near_scale = (st.nearest_enemy_xz_distance - kSelfViewEnemyNear) / 2;  // the original's literal /2
    }

    max_rate = (max_rate * near_scale) >> 12;

    std::int32_t rate = ceiling;
    if (max_rate >= 0) {
        if (max_rate <= rate) {
            rate = max_rate;
        }
    } else {
        rate = 0;
    }
    return rate;
}

void set_flags_by_move_type(const FrameInput& in, CameraState& st, std::int32_t move_type,
                            std::int32_t far_rate, bool warp) {
    const auto set = [&st](std::uint32_t bits) { st.flags |= bits; };
    const auto clear = [&st](std::uint32_t bits) { st.flags &= ~bits; };

    if (far_rate == 0) {
        switch (move_type) {
            case static_cast<std::int32_t>(CameraMove::Chase):
            case static_cast<std::int32_t>(CameraMove::Settle):
                set(camera_flags::character_visible);
                break;
            case static_cast<std::int32_t>(CameraMove::FixedAngle):
            case static_cast<std::int32_t>(CameraMove::SelfView):
            case static_cast<std::int32_t>(CameraMove::ThroughDoor):
                clear(camera_flags::character_visible);
                break;
            default:
                break;
        }
    } else {
        clear(camera_flags::character_visible);
    }

    if (move_type == static_cast<std::int32_t>(CameraMove::SelfView)) {
        set(camera_flags::warp_camera | camera_flags::warp_camera_target);

        const std::int32_t held = (st.flags >> 9) & 1;
        if ((in.extra_view_ctrl && held == 0) || (!in.extra_view_ctrl && held != 0)) {
            const std::int32_t held_previous = (st.flags >> 10) & 1;
            if (!(in.extra_view_ctrl && held_previous == 0) && !(!in.extra_view_ctrl && held_previous != 0)) {
                if (in.extra_view_mode) {
                    set(camera_flags::warp_watch);
                }
            }
        }
    }

    if (warp) {
        set(camera_flags::warp_camera | camera_flags::warp_watch | camera_flags::warp_camera_target);
    }

    if (far_rate != 0) {
        set(camera_flags::warp_camera_target);
    }
}

}  // namespace

CameraRoad default_camera_road() {
    CameraRoad road;
    road.switch_area = Area{-7680, 7680, -7680, 7680};  // +-480 m in Q4, the shipped default bits
    road.road_area = road.switch_area;
    road.flags = 0;
    road.area_size = AreaSize::Wide;
    road.road_type = RoadType::Road;
    road.move_y_type = static_cast<std::uint8_t>(CameraMove::SelfView);
    road.road_max_y = 96;   // Q4(6.0)
    road.road_min_y = -96;  // Q4(-6.0)
    road.watch_offset_y = 8;  // Q4(0.5)
    road.lens_flare = 1;
    road.camera_move = CameraMove::Chase;
    return road;
}

// The fallback zone's near-list entry (the original kept it as a static). Distance and offsets
// are zeroed; the selection recomputes the distance from the fallback road's area.
NearRoad fallback_near_road() {
    NearRoad n;
    n.road = kDefaultRoad;
    n.axis = 0;
    n.priority = 0;
    const CameraRoad def = default_camera_road();
    n.road_area = def.road_area;
    n.switch_area = def.switch_area;
    return n;
}

void CameraRig::set_roads(std::vector<CameraRoad> roads, bool roads_are_default) {
    roads_.clear();
    for (const CameraRoad& road : roads) {
        if ((road.flags & road_flags::end_marker) != 0) {
            break;
        }
        roads_.push_back(road);
        if (roads_.size() >= 4096) {
            break;  // a malformed list without an end marker must not loop forever
        }
    }
    roads_are_default_ = roads_are_default;
}

const CameraRoad& CameraRig::road(RoadRef ref) const {
    if (ref >= 0 && static_cast<std::size_t>(ref) < roads_.size()) {
        return roads_[static_cast<std::size_t>(ref)];
    }
    static const CameraRoad kDefault = default_camera_road();
    return kDefault;
}

RoadRef CameraRig::resolve_road(RoadRef ref) const {
    if (ref == kDefaultRoad && roads_are_default_ && !roads_.empty()) {
        return 0;
    }
    return ref;
}

StageAResult CameraRig::update_zone(const FrameInput& input) {
    CameraState& st = state_;
    StageAResult result;

    // 1. NPC death timers (returned to the bridge; the enemy search below reads the new values).
    result.npc_dead_time.assign(input.npcs.size(), 0);
    for (std::size_t i = 0; i < input.npcs.size(); ++i) {
        if (!input.npcs[i].present) {
            continue;
        }
        std::int32_t dead_time =
            input.npcs[i].health <= 0 ? input.npcs[i].dead_time + input.delta_time : 0;
        if (dead_time > kDeadTimeMax) {
            dead_time = kDeadTimeMax;
        }
        result.npc_dead_time[i] = dead_time;
    }

    // 2. View-button bookkeeping (only on frames with elapsed time) and screen half angles.
    if (input.delta_time != 0) {
        if ((st.flags & camera_flags::view_button) != 0) {
            st.flags |= camera_flags::view_button_previous;
        } else {
            st.flags &= ~camera_flags::view_button_previous;
        }
        if (input.view_button_held) {
            st.flags |= camera_flags::view_button;
        } else {
            st.flags &= ~camera_flags::view_button;
        }
    }
    st.screen_half_angle_x = static_cast<std::int16_t>(
        world::fixed_atan2(input.screen_width, st.geom_screen_dist) >> 1);
    st.screen_half_angle_y = static_cast<std::int16_t>(
        world::fixed_atan2(input.screen_height, st.geom_screen_dist) >> 1);

    // 3. Through-door rail: engage on request, then the per-frame distance/timer update.
    if (st.through_door_activate) {
        st.through_door.active = true;
        st.through_door.timer = 0;
        st.through_door.rail_yaw = st.chara_eye_angle_y;
        st.through_door.start = Vec3{st.chara_pos.x, st.chara_ground_y - kRailStartDropY, st.chara_pos.z};
    }
    if (st.through_door.active) {
        st.through_door.distance = world::fixed_length3(st.chara_pos.x - st.through_door.start.x, 0,
                                                        st.chara_pos.z - st.through_door.start.z);
        st.through_door.timer += input.delta_time;
    }

    // 4. Nearest enemy, 5. near-zone gathering.
    update_nearest_enemy(st, input, result.npc_dead_time);
    gather_near_roads(*this, st, input);

    // 6. Current zone with hysteresis.
    result.warp = select_current_road(*this, st);

    // 7. Move type (may retire the rail camera, which is a state change, hence before the rates).
    result.move_type = current_move_type(input, st, *this);

    // 8. Rates, derived flags, and the exclusion-radius reset a camera-target warp asks for.
    result.far_look_rate = far_look_rate(input, st, result.move_type);
    result.self_view_rate = self_view_rate(st, result.move_type);
    if ((st.flags & (camera_flags::user_camera | camera_flags::user_watch)) == 0) {
        set_flags_by_move_type(input, st, result.move_type, result.far_look_rate, result.warp);
    }
    if ((st.flags & camera_flags::warp_camera_target) != 0) {
        st.old_cam_exclusion_radius = kNoRadius;
    }

    return result;
}

std::int32_t area_distance(std::int32_t* out_x, std::int32_t* out_z,
                           std::int32_t x, std::int32_t z,
                           std::int32_t min_x, std::int32_t max_x,
                           std::int32_t min_z, std::int32_t max_z,
                           bool allow_negative) {
    std::int32_t x_distance = 0;
    std::int32_t z_distance = 0;

    if (max_x < x) {
        const std::int32_t centre = max_x - x;
        *out_x = centre;
        x_distance = -centre;
    } else if (x < min_x) {
        x_distance = min_x - x;
        *out_x = x_distance;
    } else {
        *out_x = 0;
        // Inside the span: "distance" says which edge is nearer, sign included.
        x_distance = (x >= ((max_x + min_x) >> 1)) ? (x - max_x) : (min_x - x);
    }

    if (max_z < z) {
        const std::int32_t centre = max_z - z;
        *out_z = centre;
        z_distance = -centre;
    } else if (z < min_z) {
        z_distance = min_z - z;
        *out_z = z_distance;
    } else {
        *out_z = 0;
        z_distance = (z >= ((max_z + min_z) >> 1)) ? (z - max_z) : (min_z - z);
    }

    std::int32_t distance = 0;
    if (x_distance >= 0) {
        distance = x_distance;
        if (z_distance >= 0) {
            distance += z_distance;
        }
    } else {
        distance = z_distance;
        // Both negative: keep the one closer to zero.
        if (distance < 0 && distance < x_distance) {
            distance = x_distance;
        }
    }

    if (!allow_negative && distance < 0) {
        distance = 0;
    }
    return distance;
}

}  // namespace sh::camera
