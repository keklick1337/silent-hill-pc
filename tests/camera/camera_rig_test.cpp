// The camera rig's stage A against its specification (the original's view camera): every expected
// number below is worked out by hand from the integer pipeline — the Q4->Q12 shifts, the box
// distance's branch structure, the truncating divisions and the exact tie rules. The tests do
// not link the game layer.

#include "engine/camera/camera_rig.h"

#include "framework/sh_test.h"

namespace {

namespace cam = sh::camera;

constexpr std::int32_t q12(float v) { return static_cast<std::int32_t>(v * 4096.0f); }

// A square zone of +-16 Q4 (1 m) around the origin, unless overridden.
cam::CameraRoad zone() {
    cam::CameraRoad road;
    road.switch_area = cam::Area{-16, 16, -16, 16};
    road.road_area = road.switch_area;
    return road;
}

cam::FrameInput base_input() {
    cam::FrameInput in;  // delta 0, no NPCs, no options, 320x240 screen
    in.screen_width = 320;
    in.screen_height = 240;
    return in;
}

// The enemy the camera tracks: alive, 1 m from the character on +X, active.
cam::NpcView enemy_at(std::int32_t x, std::int32_t z) {
    cam::NpcView npc;
    npc.present = true;
    npc.camera_enemy = true;
    npc.health = 1;
    npc.position = cam::Vec3{x, 0, z};
    return npc;
}

}  // namespace

// ---- the box distance ----

SH_TEST(camera_area_distance_outside_low_side) {
    std::int32_t ox = 0, oz = 0;
    // Point at x=0 against an X span [4096, 8192] (Q12) and a Z span around 0: outside on the
    // low side, so dist = 4096 - 0 = 4096 and the X offset says "4096 to the right".
    SH_CHECK(cam::area_distance(&ox, &oz, 0, 0, 4096, 8192, -4096, 4096, false) == 4096);
    SH_CHECK(ox == 4096);
    SH_CHECK(oz == 0);
}

SH_TEST(camera_area_distance_outside_high_side) {
    std::int32_t ox = 0, oz = 0;
    // x=12288 past max: the offset is negative (max - x = -4096), the distance positive 4096.
    SH_CHECK(cam::area_distance(&ox, &oz, 12288, 0, 4096, 8192, -4096, 4096, false) == 4096);
    SH_CHECK(ox == -4096);
}

SH_TEST(camera_area_distance_inside_clamped_and_merge) {
    std::int32_t ox = 0, oz = 0;
    // Inside both spans, on the far half of each: X span [4096, 8192] with x=7168 gives
    // x_dist = 7168 - 8192 = -1024; Z span [-4096, 4096] with z=2048 gives z_dist = -2048.
    // Both negative: the distance keeps the one closer to zero, -1024 — zero without the merge
    // rule, -1024 with it.
    SH_CHECK(cam::area_distance(&ox, &oz, 7168, 2048, 4096, 8192, -4096, 4096, false) == 0);
    SH_CHECK(cam::area_distance(&ox, &oz, 7168, 2048, 4096, 8192, -4096, 4096, true) == -1024);
    SH_CHECK(ox == 0);
    SH_CHECK(oz == 0);
}

SH_TEST(camera_area_distance_mixed_signs_add) {
    std::int32_t ox = 0, oz = 0;
    // X outside (4096), Z inside the near half (z=-2048 against [-4096,4096]: -2048 >= mid 0 is
    // false, so z_dist = -4096 - (-2048) = -2048): only the positive side adds, dist = 4096.
    SH_CHECK(cam::area_distance(&ox, &oz, 0, -2048, 4096, 8192, -4096, 4096, false) == 4096);
}

// ---- near-zone gathering ----

SH_TEST(camera_gathering_skips_zones_by_enemy_presence) {
    // Zone 0 demands an enemy (use_with_enemy), zone 1 is unconditional (an Effect zone), zone 2
    // is the end marker: the gathering must stop before it even though its box would match.
    std::vector<cam::CameraRoad> roads;
    cam::CameraRoad need_enemy = zone();
    need_enemy.flags = cam::road_flags::use_with_enemy;
    cam::CameraRoad effect = zone();
    effect.road_type = cam::RoadType::Effect;
    cam::CameraRoad end_marker = zone();
    end_marker.flags = cam::road_flags::end_marker;
    roads.push_back(need_enemy);
    roads.push_back(effect);
    roads.push_back(end_marker);

    cam::CameraRig rig;
    rig.set_roads(roads, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;

    // No enemy: only the unconditional zone is gathered.
    cam::FrameInput in = base_input();
    const cam::StageAResult no_enemy = rig.update_zone(in);
    SH_CHECK(rig.state().near_road_count == 1);
    SH_CHECK(rig.state().near_roads[0].road == 1);
    SH_CHECK(rig.state().near_roads[0].priority == 1);  // Effect -> 1
    SH_CHECK(rig.state().near_roads[0].axis == 0);      // equal extents -> runs along Z
    SH_CHECK(rig.state().near_roads[0].distance == 0);  // dead centre: clamped inside distance
    SH_CHECK(rig.state().near_roads[0].offset_x == 0);
    SH_CHECK(rig.state().near_roads[0].offset_z == 0);
    SH_CHECK(rig.state().nearest_enemy == -1);
    SH_CHECK(rig.state().nearest_enemy_xz_distance == q12(15.0f));  // 61440, the search maximum
    (void)no_enemy;

    // With an enemy in range the demand flips: the enemy-requiring zone engages too.
    cam::FrameInput with_enemy = base_input();
    with_enemy.npcs.push_back(enemy_at(4096, 0));
    rig.update_zone(with_enemy);
    SH_CHECK(rig.state().near_road_count == 2);
    SH_CHECK(rig.state().near_roads[0].road == 0);
    SH_CHECK(rig.state().near_roads[1].road == 1);
    SH_CHECK(rig.state().nearest_enemy == 0);
    SH_CHECK(rig.state().nearest_enemy_xz_distance == 4096);  // exact 1 m root
}

SH_TEST(camera_gathering_merge_allows_negative_distance) {
    std::vector<cam::CameraRoad> roads;
    cam::CameraRoad merged = zone();
    merged.flags = cam::road_flags::merge_road;
    roads.push_back(merged);

    cam::CameraRig rig;
    rig.set_roads(roads, false);
    rig.state().chara_pos = cam::Vec3{2048, 0, 0};  // upper half of the +-1 m box
    rig.state().geom_screen_dist = 240;

    rig.update_zone(base_input());
    // Inside, on the far half of X (x_dist = 2048 - 4096 = -2048) and of Z (z_dist = -4096):
    // both negative, distance keeps the one closer to zero and the merge rule lets it through.
    SH_CHECK(rig.state().near_road_count == 1);
    SH_CHECK(rig.state().near_roads[0].distance == -2048);
}

SH_TEST(camera_gathering_anchor_shift_moves_road_area) {
    std::vector<cam::CameraRoad> roads;
    roads.push_back(zone());

    cam::CameraRig rig;
    rig.set_roads(roads, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;

    cam::FrameInput in = base_input();
    cam::RoadAnchorShift shift;
    shift.road = 0;
    shift.x_shift = 1024;  // a quarter metre into the room
    in.road_anchor_shifts.push_back(shift);

    rig.update_zone(in);
    // The stored road area carries the shift; the switch area does not.
    SH_CHECK(rig.state().near_roads[0].road_area.min_x == -16 + 4);  // 1024 Q12 = 4 Q4 units
    SH_CHECK(rig.state().near_roads[0].road_area.max_x == 16 + 4);
    SH_CHECK(rig.state().near_roads[0].switch_area.min_x == -16);
}

// ---- best-zone choice ----

SH_TEST(camera_best_road_event_wins_at_equal_distance) {
    std::vector<cam::CameraRoad> roads;
    cam::CameraRoad event_zone = zone();
    event_zone.road_type = cam::RoadType::Event;
    cam::CameraRoad plain_zone = zone();
    roads.push_back(event_zone);
    roads.push_back(plain_zone);

    cam::CameraRig rig;
    rig.set_roads(roads, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;
    rig.update_zone(base_input());

    // Both at distance 0; the event round renews first (dist <= 0) and the road round cannot
    // displace it (its distance is not < 0).
    SH_CHECK(rig.state().current_road.road == 0);
    SH_CHECK(rig.state().current_road.priority == 2);
}

SH_TEST(camera_best_road_closer_zone_displaces_event) {
    std::vector<cam::CameraRoad> roads;
    cam::CameraRoad event_zone = zone();
    event_zone.road_type = cam::RoadType::Event;
    event_zone.switch_area = cam::Area{16, 24, -16, 16};  // 1..1.5 m out on +X
    cam::CameraRoad plain_zone = zone();
    roads.push_back(event_zone);
    roads.push_back(plain_zone);

    cam::CameraRig rig;
    rig.set_roads(roads, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;
    rig.update_zone(base_input());

    // Event distance: X outside low side (4096), Z inside (clamped) -> 4096. The plain zone at
    // 0 takes it in the road round: 0 > 0 is false for the event's hold, but 4096 > 0 and
    // 0 < 4096 renews.
    SH_CHECK(rig.state().current_road.road == 1);
}

SH_TEST(camera_default_list_fallback_is_entry_zero) {
    // The game's no-zone-list case: the fallback zone and list entry 0 are the same zone, so a
    // current road of kDefaultRoad must match near entry 0 (a pointer identity in the original).
    cam::CameraRig rig;
    rig.set_roads({cam::default_camera_road()}, true);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;
    SH_CHECK(rig.state().current_road.road == cam::kDefaultRoad);

    const cam::StageAResult result = rig.update_zone(base_input());
    SH_CHECK(rig.state().near_road_count == 1);
    SH_CHECK(rig.state().current_road.road == 0);  // resolved, not left as kDefaultRoad
    SH_CHECK(result.warp == false);
    SH_CHECK(result.move_type == static_cast<std::int32_t>(cam::CameraMove::Chase));
}

// ---- current-zone hysteresis ----

SH_TEST(camera_hysteresis_keeps_old_zone_inside_margin) {
    std::vector<cam::CameraRoad> roads;
    cam::CameraRoad plain_zone = zone();                            // old, distance 0 at centre
    cam::CameraRoad event_zone = zone();                            // new, 1 m out
    event_zone.road_type = cam::RoadType::Event;
    event_zone.switch_area = cam::Area{16, 24, -16, 16};
    roads.push_back(plain_zone);
    roads.push_back(event_zone);

    cam::CameraRig rig;
    rig.set_roads(roads, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().chara_move_angle_y = 0;
    rig.state().geom_screen_dist = 240;

    // First frame takes the plain zone (event 4096 away loses to 0), giving the old entry.
    rig.update_zone(base_input());
    SH_CHECK(rig.state().current_road.road == 0);

    // Second frame: the event zone is the best new zone (still 4096), but the old zone is at
    // distance 0 and keep_frame = 0 - 1433 = -1433 <= 4096, so the old zone is kept.
    rig.update_zone(base_input());
    SH_CHECK(rig.state().current_road.road == 0);
}

SH_TEST(camera_hysteresis_higher_priority_steals_with_warp) {
    std::vector<cam::CameraRoad> roads;
    cam::CameraRoad plain_zone = zone();
    plain_zone.switch_area = cam::Area{-16, 0, -16, 16};  // old zone off to -X
    plain_zone.flags = cam::road_flags::warp_out;
    cam::CameraRoad event_zone = zone();                  // new zone right on the character
    event_zone.road_type = cam::RoadType::Event;
    event_zone.flags = cam::road_flags::warp_in;
    roads.push_back(plain_zone);
    roads.push_back(event_zone);

    cam::CameraRig rig;
    rig.set_roads(roads, false);
    rig.state().chara_pos = cam::Vec3{6144, 0, 0};  // 1.5 m out of the old zone's box
    rig.state().geom_screen_dist = 240;

    const cam::StageAResult result = rig.update_zone(base_input());
    // Old zone distance 6144 (outside high side), best new is the event zone at 2048 with the
    // higher priority. keep_frame = 6144 - 1433 = 4711 and 2048 < 4711, so the old zone loses:
    // the event takes over, warp_in and warp_out both fire.
    SH_CHECK(rig.state().current_road.road == 1);
    SH_CHECK(result.warp == true);
}

// ---- through-door rail ----

SH_TEST(camera_through_door_start_and_update) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    rig.state().chara_pos = cam::Vec3{1000, 2000, 3000};
    rig.state().chara_ground_y = 2000;
    rig.state().chara_eye_angle_y = 1024;  // 90 degrees
    rig.state().geom_screen_dist = 240;
    rig.state().through_door_activate = true;

    cam::FrameInput in = base_input();
    in.delta_time = 1365;  // ~1/3 s
    const cam::StageAResult result = rig.update_zone(in);

    SH_CHECK(rig.state().through_door.active == true);
    SH_CHECK(rig.state().through_door.timer == 1365);
    SH_CHECK(rig.state().through_door.rail_yaw == 1024);
    SH_CHECK(rig.state().through_door.start.x == 1000);
    SH_CHECK(rig.state().through_door.start.y == 2000 - 7987);  // 1.95 m below the feet
    SH_CHECK(rig.state().through_door.start.z == 3000);
    SH_CHECK(rig.state().through_door.distance == 0);
    SH_CHECK(result.move_type == static_cast<std::int32_t>(cam::CameraMove::ThroughDoor));
    // No enemy: the search maximum, which also means the rail cannot end this frame.
    SH_CHECK(rig.state().nearest_enemy_xz_distance == q12(15.0f));
}

SH_TEST(camera_through_door_far_look_rate_fades_with_distance) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    rig.state().chara_pos = cam::Vec3{1000, 2000, 3000 + 4608};  // 1.125 m past the start
    rig.state().chara_ground_y = 2000;
    rig.state().chara_eye_angle_y = 0;  // facing dead away from the start: heading offset 0
    rig.state().geom_screen_dist = 240;
    rig.state().through_door.active = true;
    rig.state().through_door.timer = 100;
    rig.state().through_door.rail_yaw = 0;
    rig.state().through_door.start = cam::Vec3{1000, 2000, 3000};
    rig.state().through_door.distance = 4608;

    const cam::StageAResult result = rig.update_zone(base_input());
    SH_CHECK(result.move_type == static_cast<std::int32_t>(cam::CameraMove::ThroughDoor));
    // base = 3686 - (4608 * 3686) / 9420 = 3686 - 1803 = 1883; heading offset 0 keeps it.
    SH_CHECK(result.far_look_rate == 1883);
    // Rail-camera self-view ceiling is 5.6 in Q8 = 1433, full at head distance 0.
    SH_CHECK(result.self_view_rate == 1433);
}

SH_TEST(camera_through_door_ends_by_reach_and_timer) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    rig.state().geom_screen_dist = 240;

    // Past 2.3 m: the rail retires and the road's move type takes over. (The rail reach is the
    // APPROXIMATED root: 20000 on Z pre-shifts by 4 and measures 1247 << 4 = 19952, > 9420.)
    rig.state().chara_pos = cam::Vec3{0, 0, 20000};
    rig.state().chara_ground_y = 0;
    rig.state().chara_eye_angle_y = 0;
    rig.state().through_door.active = true;
    rig.state().through_door.timer = 100;
    rig.state().through_door.rail_yaw = 0;
    rig.state().through_door.start = cam::Vec3{0, 0, 0};
    rig.state().through_door.distance = 9421;
    cam::StageAResult result = rig.update_zone(base_input());
    SH_CHECK(rig.state().through_door.active == false);
    SH_CHECK(rig.state().through_door.timer == 0);
    SH_CHECK(rig.state().through_door.distance == 19952);
    SH_CHECK(result.move_type == static_cast<std::int32_t>(cam::CameraMove::Chase));

    // 1.2 s elapsed with an enemy closer than 1.2 m: also ends. (The enemy distance is this
    // frame's search result, so the enemy must actually be near.)
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().through_door.active = true;
    rig.state().through_door.timer = 4916;
    rig.state().through_door.distance = 0;
    cam::FrameInput enemy_near = base_input();
    enemy_near.npcs.push_back(enemy_at(4096, 0));
    result = rig.update_zone(enemy_near);
    SH_CHECK(rig.state().through_door.active == false);
}

// ---- nearest enemy ----

SH_TEST(camera_nearest_enemy_prefers_active) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;

    cam::FrameInput in = base_input();
    in.npcs.resize(3);
    in.npcs[1] = enemy_at(4096, 0);
    in.npcs[1].passive = true;   // nearer, but idle
    in.npcs[2] = enemy_at(8192, 0);  // farther, active

    rig.update_zone(in);
    SH_CHECK(rig.state().nearest_enemy == 2);
    SH_CHECK(rig.state().nearest_enemy_xz_distance == 8192);

    // Everyone idle: the plain nearest wins.
    in.npcs[2].passive = true;
    rig.update_zone(in);
    SH_CHECK(rig.state().nearest_enemy == 1);
    SH_CHECK(rig.state().nearest_enemy_xz_distance == 4096);

    // An aimed-at passive enemy is active again.
    in.npcs[1].is_target = true;
    in.player_aiming = true;
    rig.update_zone(in);
    SH_CHECK(rig.state().nearest_enemy == 1);

    // Excluded enemies never count.
    in.player_aiming = false;
    in.npcs[1].excluded = true;
    in.npcs[2].excluded = true;
    rig.update_zone(in);
    SH_CHECK(rig.state().nearest_enemy == -1);
    SH_CHECK(rig.state().nearest_enemy_xz_distance == q12(15.0f));
}

SH_TEST(camera_nearest_enemy_skips_long_dead) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;

    cam::FrameInput in = base_input();
    in.npcs.push_back(enemy_at(4096, 0));
    in.npcs[0].health = -1;
    in.npcs[0].dead_time = 6145;  // dead longer than the 1.5 s grace

    const cam::StageAResult result = rig.update_zone(in);
    SH_CHECK(rig.state().nearest_enemy == -1);
    // The timer keeps accumulating to the 10 s clamp: 6145 + 0 = 6145 (delta 0).
    SH_CHECK(result.npc_dead_time[0] == 6145);
}

SH_TEST(camera_dead_timer_clamps_and_resets) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;

    cam::FrameInput in = base_input();
    in.delta_time = 4096;  // 1 s
    in.npcs.resize(2);
    in.npcs[0] = enemy_at(4096, 0);
    in.npcs[0].health = -1;
    in.npcs[0].dead_time = 40000;  // 40 s already: clamps at 10 s despite the +1 s
    in.npcs[1] = enemy_at(-4096, 0);
    in.npcs[1].health = 1;  // alive: the timer resets to 0

    const cam::StageAResult result = rig.update_zone(in);
    SH_CHECK(result.npc_dead_time[0] == q12(10.0f));
    SH_CHECK(result.npc_dead_time[1] == 0);
}

// ---- far-look and self-view rates ----

SH_TEST(camera_far_look_rate_chase_follows_button) {
    cam::CameraRig rig;
    rig.set_roads({zone()}, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;

    cam::FrameInput in = base_input();
    in.delta_time = 100;
    in.view_button_held = true;
    const cam::StageAResult on = rig.update_zone(in);
    SH_CHECK(on.move_type == static_cast<std::int32_t>(cam::CameraMove::Chase));
    SH_CHECK(on.far_look_rate == 4096);
    SH_CHECK((rig.state().flags & cam::camera_flags::view_button) != 0);

    in.view_button_held = false;
    const cam::StageAResult off = rig.update_zone(in);
    SH_CHECK(off.far_look_rate == 0);
}

SH_TEST(camera_self_view_rate_by_head_distance) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;
    rig.state().nearest_enemy_xz_distance = 16385;  // past 4 m: full near scale
    // No roads gather: the search sets nearest_enemy_xz_distance to 61440 anyway.

    cam::FrameInput in = base_input();
    in.extra_view_mode = true;
    in.extra_view_ctrl = true;  // view engages while the button is NOT held
    in.npcs.resize(1);          // no enemies: nothing overrides the self view

    // Head 512 Q12 away (32 in Q8, inside 0.5 m): full ceiling 4096. The search sets the enemy
    // distance to the 15 m maximum, so the near scale is the full 4096.
    rig.state().cam_pos = cam::Vec3{0, 0, 0};
    rig.state().chara_head_pos = cam::Vec3{512, 0, 0};
    const cam::StageAResult near_head = rig.update_zone(in);
    SH_CHECK(near_head.move_type == static_cast<std::int32_t>(cam::CameraMove::SelfView));
    SH_CHECK(near_head.self_view_rate == 4096);

    // Head 3072 Q12 away (192 in Q8 after the component shift — a perfect root, so the
    // approximated square root stays exact): inside the 0.5..1.2 m falloff,
    // max = 4096 * (307 - 192) / 179 = 2631, near scale stays 4096 (enemy at the 15 m maximum).
    rig.state().chara_head_pos = cam::Vec3{3072, 0, 0};
    const cam::StageAResult mid_head = rig.update_zone(in);
    SH_CHECK(mid_head.self_view_rate == 2631);
}

// ---- view-button bookkeeping and screen half angles ----

SH_TEST(camera_view_button_edge_and_half_angles) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;
    rig.state().flags = cam::camera_flags::view_button;  // was held last frame

    cam::FrameInput in = base_input();
    in.screen_width = 240;
    in.screen_height = 240;
    in.delta_time = 100;  // a frame with elapsed time: the bookkeeping runs
    in.view_button_held = false;

    rig.update_zone(in);
    // Previous copies the held state as it was at frame start; the current one follows the pad.
    SH_CHECK((rig.state().flags & cam::camera_flags::view_button) == 0);
    SH_CHECK((rig.state().flags & cam::camera_flags::view_button_previous) != 0);
    // A square screen against an equal projection distance: half of 45 degrees, 512 / 2 = 256.
    SH_CHECK(rig.state().screen_half_angle_x == 256);
    SH_CHECK(rig.state().screen_half_angle_y == 256);

    // A frozen frame (delta 0) changes neither flag.
    rig.update_zone(base_input());
    SH_CHECK((rig.state().flags & cam::camera_flags::view_button) == 0);
    SH_CHECK((rig.state().flags & cam::camera_flags::view_button_previous) != 0);
}

// ---- the exclusion radius reset a camera-target warp asks for ----

SH_TEST(camera_warp_target_resets_exclusion_radius) {
    cam::CameraRig rig;
    rig.set_roads({}, false);
    rig.state().chara_pos = cam::Vec3{0, 0, 0};
    rig.state().geom_screen_dist = 240;
    rig.state().old_cam_exclusion_radius = 4096;

    // Self view sets warp_camera_target, which resets the radius to the no-value marker.
    cam::FrameInput in = base_input();
    in.extra_view_mode = true;
    in.extra_view_ctrl = true;
    rig.update_zone(in);
    SH_CHECK((rig.state().flags & cam::camera_flags::warp_camera_target) != 0);
    SH_CHECK(rig.state().old_cam_exclusion_radius == -1);
}
