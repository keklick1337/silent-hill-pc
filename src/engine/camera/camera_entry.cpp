// The camera's C entry points: the frame update vcExecCamera, the camera
// API between frames (each one CameraRig method on the game camera), the camera driver (map-start
// camera, the subject read from Harry, the developer camera modes) and the stereo balance of
// positional sound. The game state comes from the session and the camera host (camera_host.h).
// Arguments use the engine's layout-identical types; flags passed as C booleans are 4-byte words.

#include <cstdint>
#include <string>
#include <vector>

#include "engine/audio/positional_sound.h"
#include "engine/camera/camera_config.h"
#include "engine/camera/camera_driver.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/camera_matrix.h"
#include "engine/camera/game_camera.h"
#include "engine/camera/precise_view.h"
#include "engine/camera/self_view_gaze.h"
#include "engine/camera/view_space.h"
#include "engine/camera/view_system.h"
#include "engine/game_state/session.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_runtime.h"

namespace cam = ::sh::camera;

namespace {

using sh::game::session;

// The camera's move kinds by number (CameraMove).
constexpr std::int32_t kMoveChase = static_cast<std::int32_t>(cam::CameraMove::Chase);
constexpr std::int32_t kMoveFixedAngle = static_cast<std::int32_t>(cam::CameraMove::FixedAngle);

// Set once the camera system is started on a map: from then on the frame reads the running map's
// camera zones (sh::maps::map_runtime(), loaded with the map) anew every time.
bool s_roads_set = false;

// The developer camera driver: its mode, the last frame's smooth-move kind and the elevation rate
// of the follow subject; the reference point of the orbit modes.
struct DriverState {
    std::uint32_t mode = 0;
    std::uint8_t smooth_move = 0;
    std::int16_t elevation_rate = 0;
    cam::Vec3 reference;
};
DriverState s_driver;

int s_last_move_type = -1;

cam::CameraState& camera_state() {
    return cam::game_camera().state();
}

cam::CameraRig& rig() {
    return cam::game_camera().rig;
}

// The zones of the list in force.
void read_roads(std::vector<cam::CameraRoad>& out) {
    out.clear();
    if (s_roads_set) {
        out = sh::maps::map_runtime().camera_zones();
    }
}

// A zone the state still names from an earlier frame is that entry of the list as it is now; an
// entry the list no longer has is the fallback zone. Running on the fallback list, the fallback
// zone and entry 0 are one zone, kept as entry 0.
cam::RoadRef current_ref(cam::RoadRef ref, std::size_t count, bool roads_default) {
    if (roads_default && ref == cam::kDefaultRoad) {
        return 0;
    }
    if (ref < 0 || static_cast<std::size_t>(ref) >= count) {
        return cam::kDefaultRoad;
    }
    return ref;
}

void refresh_road_refs(cam::CameraState& st, std::size_t count, bool roads_default) {
    for (int i = 0; i < st.near_road_count && i < cam::kNearRoadCapacity; ++i) {
        st.near_roads[i].road = current_ref(st.near_roads[i].road, count, roads_default);
    }
    st.current_road.road = current_ref(st.current_road.road, count, roads_default);
}

// The zone the camera is in now (the engine's fallback zone when none of the map's).
cam::CameraRoad current_road() {
    const cam::RoadRef ref = camera_state().current_road.road;
    const std::vector<cam::CameraRoad>& zones = sh::maps::map_runtime().camera_zones();
    if (ref == cam::kDefaultRoad || !s_roads_set || static_cast<std::size_t>(ref) >= zones.size()) {
        return cam::default_camera_road();
    }
    return zones[static_cast<std::size_t>(ref)];
}

// Harry's animations the self-view gaze tells apart, by their names in his character.json.
const cam::SelfViewGazeAnims& harry_gaze_anims() {
    static const cam::SelfViewGazeAnims anims = [] {
        cam::CameraHost& host = cam::camera_host();
        cam::SelfViewGazeAnims a;
        a.walk_forward = host.harry_clip("walk_forward", a.walk_forward);
        a.run_forward = host.harry_clip("run_forward", a.run_forward);
        a.turn_left = host.harry_clip("turn_left", a.turn_left);
        a.turn_right = host.harry_clip("turn_right", a.turn_right);
        a.look_around = host.harry_clip("look_around", a.look_around);
        a.fall_forward = host.harry_clip("fall_forward", a.fall_forward);
        a.fall_backward = host.harry_clip("fall_backward", a.fall_backward);
        a.idle = host.harry_clip("idle", a.idle);
        a.idle_exhausted = host.harry_clip("idle_exhausted", a.idle_exhausted);
        return a;
    }();
    return anims;
}

// Where Harry's head looks in the self view: his record and head bone, through the engine rule.
cam::Angles harry_self_view_gaze(const cam::GazeQuery& q) {
    cam::CameraHost& host = cam::camera_host();
    const cam::HarryView harry = host.harry();
    cam::FrameRecord* head = host.harry_head();
    cam::SelfViewGazeInput in;
    in.anim_status = harry.anim_status;
    in.body_yaw = harry.yaw;
    in.head = cam::matrix_angles(head->world);
    in.enemy_near = q.enemy_near;
    in.pitch = q.pitch;
    return cam::self_view_gaze(in, harry_gaze_anims());
}

cam::GroundSample ground_at(std::int32_t x, std::int32_t z) {
    return cam::camera_host().ground_at(x, z);
}

// The frame inputs: the characters, pad and options, the motion defaults and the map rules.
void capture_input(cam::FrameInput& in, const cam::GameCamera& camera, const std::vector<cam::CameraRoad>& roads) {
    const cam::CameraState& st = camera.state();
    const sh::game::Session& s = session();
    cam::CameraHost& host = cam::camera_host();
    in = cam::FrameInput{};
    in.delta_time = host.delta_time();
    // Scripts may retune the default motion (sh.camera.set_user_max_speed_*).
    in.tuning.follow_watch = camera.default_watch;
    in.tuning.follow_camera = camera.user_camera;
    in.view_button_held = (s.game.pads[0].held & s.game.settings.binds.view) != 0;
    in.extra_view_mode = s.game.settings.self_view != 0;
    in.extra_view_ctrl = s.game.settings.view_reverse != 0;
    in.screen_width = s.game.screen_width;
    in.screen_height = s.game.screen_height;
    in.combat_suppresses_enemies = s.sys_flag(sh::game::sys_flag::kSceneLock);
    in.player_aiming = s.system.combat.aiming > 0;

    host.npcs(in.npcs);
    for (std::size_t i = 0; i < in.npcs.size(); ++i) {
        in.npcs[i].is_target = static_cast<int>(i) == static_cast<int>(s.system.target_npc);
    }
    in.ground_at = ground_at;
    in.self_view_gaze = harry_self_view_gaze;

    const cam::MapCameraRules& rules = cam::camera_config().map_rules;
    const cam::MapPlace place = host.map_place();
    in.wide_shot = cam::wide_shot(rules, place, host.hor_plus_widths(), st.geom_screen_dist);
    in.self_view_blocked = cam::self_view_blocked(rules, place, st.chara_pos.x, st.chara_pos.z);
    in.tight_doorways = cam::tight_doorways(rules, place);
    in.road_anchor_shifts = cam::road_anchor_shifts(rules, place, roads);
}

}  // namespace

extern "C" {

// ---- the frame ----

std::int32_t vcExecCamera(void) {
    cam::GameCamera& camera = cam::game_camera();
    if (!camera.state().view_active) {
        return kMoveChase;
    }

    static std::vector<cam::CameraRoad> roads;
    static cam::FrameInput input;
    read_roads(roads);
    // A map always gives its own list (empty or not); the fallback list is never in force.
    const bool roads_default = false;
    refresh_road_refs(camera.state(), roads.size(), roads_default);
    capture_input(input, camera, roads);
    camera.rig.set_roads(roads, roads_default);
    const cam::FrameResult result = camera.rig.update(input);

    cam::CameraHost& host = cam::camera_host();
    for (std::size_t i = 0; i < input.npcs.size() && i < result.zone.npc_dead_time.size(); ++i) {
        if (input.npcs[i].present) {
            host.set_npc_dead_time(i, result.zone.npc_dead_time[i]);
        }
    }
    cam::set_projection_distance(camera.state().geom_screen_dist);

    // A fixed-angle shot gets the PC projection-centre correction.
    host.set_fixed_angle_shot(result.zone.move_type == kMoveFixedAngle);
    s_last_move_type = result.zone.move_type;

    cam::ViewSystem& view = cam::view_system();
    if (result.view.view.kind == cam::ViewOutput::Kind::HeadLookAt) {
        // Harry's head bone, 5 cm up and 30 cm ahead of it, turned back toward the body.
        view.set_frame(host.harry_head(), cam::orbit_matrix(cam::Vec3{0, -204, 1228}, 2048, 0, -819, 4096));
    } else {
        view.set_frame(nullptr, result.view.view.matrix);
    }
    const cam::Matrix& rig_matrix = camera.state().cam_matrix;
    const cam::Matrix& out = result.view.view.matrix;
    bool direct = result.view.view.kind == cam::ViewOutput::Kind::DirectMatrix;
    for (int r = 0; r < 3 && direct; ++r) {
        direct = out.t[r] == rig_matrix.t[r] && out.m[r][0] == rig_matrix.m[r][0] && out.m[r][1] == rig_matrix.m[r][1] &&
                 out.m[r][2] == rig_matrix.m[r][2];
    }
    cam::update_precise_camera(camera.state(), out, direct);
    return result.view.smooth_move_type;
}

// ---- the camera API between frames ----

void vcInitVCSystem(void) {
    const cam::CameraConfig& config = cam::camera_api_config();
    s_roads_set = true;
    rig().init_system(config.watch_max_y);
}

void vcStartCameraSystem(void) {
    rig().start_system(static_cast<std::int16_t>(session().game.screen_height));
}

void vcEndCameraSystem(void) {
    rig().stop_system();
}

std::int32_t Vc_LensFlareTypeGet(void) {
    return current_road().lens_flare;
}

void Vc_LookAtPositionYSet(std::int32_t lookAtPosY) {
    rig().set_watch_position_y(lookAtPosY);
}

std::int32_t Vc_LookAtPositionYGet(void) {
    return camera_state().watch_position_y;
}

void vcSetFirstCamWork(cam::Vec3* cam_pos, std::int16_t chara_eye_ang_y, std::uint32_t use_through_door_cam_f) {
    // The original flag byte kept the low 8 bits of the caller's value (callers pass a raw system flag
    // bit), so its truth is that of those bits.
    const bool through_door = static_cast<std::uint8_t>(use_through_door_cam_f) != 0;
    rig().set_first_camera(*cam_pos, chara_eye_ang_y, through_door);
}

// A look-at matrix attached to a frame of the world: the frame's world matrix composed with the
// local rotation and position.
void Vc_LookAtFrameSet(cam::FrameRecord* frame, cam::PackedAngles* rot, cam::Vec3* pos) {
    const cam::Angles angles{rot->x, rot->y, rot->z};
    rig().set_look_at_matrix(cam::attached_look_at(cam::frame_world(frame), angles, *pos));
}

void vcWorkSetFlags(std::uint32_t enable, std::uint32_t disable) {
    rig().set_flags(enable, disable);
}

std::int32_t Vc_LookAtOffsetYMaxSet(std::int32_t lookAtOffsetYMax) {
    return rig().set_watch_max_y(lookAtOffsetYMax);
}

void vcUserWatchTarget(cam::Vec3* watch_tgt_pos, cam::WatchMoveParams* watch_prm_p, std::uint32_t warp_watch_f) {
    const cam::WatchMoveParams params = watch_prm_p != nullptr ? *watch_prm_p : cam::game_camera().default_watch;
    rig().user_watch_target(*watch_tgt_pos, params, warp_watch_f != 0);
}

void vcUserCamTarget(cam::Vec3* cam_tgt_pos, cam::CamMoveParams* cam_prm_p, std::uint32_t warp_cam_f) {
    const cam::CamMoveParams params = cam_prm_p != nullptr ? *cam_prm_p : cam::game_camera().user_camera;
    rig().user_camera_target(*cam_tgt_pos, params, warp_cam_f != 0);
}

void vcChangeProjectionValue(std::int16_t scr_y) {
    rig().set_projection(scr_y);
}

void Vc_UpdateLookAtPointSet(void) {
    rig().request_look_at_point_update();
}

void vcGetNowWatchPos(cam::Vec3* watch_pos) {
    const cam::CameraState& st = camera_state();
    *watch_pos = cam::now_watch_position(st.cam_pos, st.watch_target_pos, st.cam_rotation);
}

void vcGetNowCamPos(cam::Vec3* cam_pos) {
    *cam_pos = camera_state().cam_pos;
}

void vcReturnPreAutoCamWork(std::uint32_t warp_f) {
    // While the cinematic letterbox zoom runs (border states 18..23 outside a cutscene), the
    // letterbox owns the projection: a cutscene's per-frame return to the automatic camera (the
    // alley match scene's camera switch at step 11) would otherwise reset the zoom set earlier in
    // the frame and pop the FOV out while the bars are still up. The letterbox ramps it back
    // itself as the bars fade.
    const sh::game::Session& s = session();
    const std::int32_t border = s.system.flow.cutscene_border;
    const bool zoom_owns_projection = border >= 18 && border <= 23 && !s.sys_flag(sh::game::sys_flag::kCutscene);
    rig().return_to_auto_camera(warp_f != 0, zoom_owns_projection, static_cast<std::int16_t>(s.game.screen_height));
}

void vcSetSubjChara(cam::Vec3* chara_pos, std::int32_t chara_bottom_y, std::int32_t chara_top_y,
                    std::int32_t chara_grnd_y, cam::Vec3* chara_head_pos, std::int16_t chara_mv_spd,
                    std::int32_t chara_mv_ang_y, std::int16_t chara_ang_spd_y, std::int16_t chara_eye_ang_y,
                    std::int16_t chara_eye_ang_wy, std::int32_t chara_watch_xz_r) {
    cam::Subject s;
    s.position = *chara_pos;
    s.bottom_y = chara_bottom_y;
    s.top_y = chara_top_y;
    s.ground_y = chara_grnd_y;
    s.head = *chara_head_pos;
    s.move_speed = chara_mv_spd;
    s.move_angle_y = chara_mv_ang_y;
    s.angle_speed_y = chara_ang_spd_y;
    s.eye_angle_y = chara_eye_ang_y;
    s.eye_angle_wy = chara_eye_ang_wy;
    s.watch_xz_radius = chara_watch_xz_r;
    rig().set_subject(s);
}

std::int32_t Vc_VectorMagnitudeCalc(std::int32_t posX, std::int32_t posY, std::int32_t posZ) {
    return cam::vector_length(posX, posY, posZ);
}

// ---- the camera driver ----

void vcSetCameraUseWarp(const cam::Vec3* chr_pos, std::int16_t chr_ang_y) {
    sh::game::Session& s = session();
    cam::Vec3 cam_pos = cam::first_camera_position(*chr_pos, chr_ang_y, cam::camera_api_config());
    // The system flag is consumed: a rail start applies to this placement only.
    vcSetFirstCamWork(&cam_pos, chr_ang_y, s.system.process.sys_flags & sh::game::sys_flag::kOnCameraRail);
    s.system.process.sys_flags &= ~sh::game::sys_flag::kOnCameraRail;
}

void vcInitCamera(void* /*map_overlay_ptr: the zones are the running map's*/, const cam::Vec3* chr_pos) {
    sh::game::Session& s = session();
    s_driver.smooth_move = static_cast<std::uint8_t>(kMoveChase);
    s_driver.elevation_rate = 0;
    s_driver.mode = 0;

    vcSetCameraUseWarp(chr_pos, s.system.camera_angle_y);
    cam::set_projection_distance(static_cast<std::int16_t>(s.game.screen_height));
    cam::view_system().init();
    vcInitVCSystem();
    vcStartCameraSystem();

    // The developer orbit starts level, at its configured radius.
    s.system.camera_angle_z = 0;
    s.system.camera_radius_xz = cam::camera_api_config().debug.start_radius;
    s.system.camera_height = 0;
}

std::int32_t vcRetCamMvSmoothF(void) {
    return s_driver.smooth_move;
}

void Vc_CameraElevationRateLockSet(std::uint32_t isUnlocked) {
    s_driver.elevation_rate = isUnlocked ? 4096 : 0;
}

void vcSetEvCamRate(std::int16_t ev_cam_rate) {
    s_driver.elevation_rate = ev_cam_rate;
}

void Vc_UpdateLookAtPointSetAlt(void) {
    Vc_UpdateLookAtPointSet();
}

void vcMakeHeroHeadPos(cam::Vec3* head_pos) {
    *head_pos = cam::head_point(cam::frame_world(cam::camera_host().harry_head()), cam::camera_api_config().subject);
}

}  // extern "C"

namespace {

// The camera driver's modes: 0 follows Harry, 1 orbits a reference, 2 flies by pad, 3 orbits
// Harry's head; any other value restarts at 0 beside Harry.
enum DriverMode : std::uint32_t { kFollow = 0, kOrbitReference = 1, kFly = 2, kOrbitHead = 3 };

cam::Orbit orbit_now() {
    const sh::game::SystemBlock& sys = session().system;
    cam::Orbit o;
    o.radius = sys.camera_radius_xz;
    o.yaw = sys.camera_angle_y;
    o.height = sys.camera_height;
    return o;
}

void store_orbit(const cam::Orbit& o) {
    sh::game::SystemBlock& sys = session().system;
    sys.camera_radius_xz = o.radius;
    sys.camera_angle_y = static_cast<std::int16_t>(o.yaw);
    sys.camera_height = o.height;
}

cam::Vec3 orbit_reference(const cam::OrbitInput& in) {
    cam::CameraHost& host = cam::camera_host();
    cam::Orbit orbit = orbit_now();
    const cam::HarryView harry = host.harry();
    const cam::Vec3 ref =
        cam::adjust_orbit(orbit, in, host.vblanks(), harry.position, harry.yaw, cam::camera_api_config().debug);
    store_orbit(orbit);
    return ref;
}

void fly_camera() {
    cam::ViewSystem& view = cam::view_system();
    const cam::PackedAngles& a = view.angles();
    const std::uint32_t held = session().game.pads[1].held;
    // Bits of the pad word (PadState::held).
    constexpr std::uint32_t kTriangle = 1u << 12, kCircle = 1u << 13, kCross = 1u << 14, kSquare = 1u << 15;
    constexpr std::uint32_t kStickUp = 1u << 24, kStickRight = 1u << 25, kStickDown = 1u << 26,
                            kStickLeft = 1u << 27;
    cam::FlyInput in;
    in.up = (held & kStickUp) != 0;
    in.down = (held & kStickDown) != 0;
    in.left = (held & kStickLeft) != 0;
    in.right = (held & kStickRight) != 0;
    in.forward = (held & kTriangle) != 0;
    in.backward = (held & kCross) != 0;
    in.shift = (held & kCircle) != 0;
    // Square is the sprint bind (Left Shift in the alt-camera scheme): fly faster.
    in.boost = (held & kSquare) != 0;
    const cam::FlyResult r = cam::fly_by_pad(view.position(), cam::Angles{a.x, a.y, a.z}, in,
                                             cam::camera_host().vblanks(), cam::camera_api_config().debug);
    view.set_frame(nullptr, r.view);
    if (r.moved) {
        s_driver.reference = r.reference;
        store_orbit(r.orbit);
    }
}

}  // namespace

extern "C" {

void vcMoveAndSetCamera(std::uint32_t in_connect_f, std::uint32_t change_debug_mode, std::uint32_t for_f,
                        std::uint32_t back_f, std::uint32_t right_f, std::uint32_t left_f, std::uint32_t up_f,
                        std::uint32_t down_f) {
    cam::CameraHost& host = cam::camera_host();
    const cam::CameraConfig& config = cam::camera_api_config();
    cam::ViewSystem& view = cam::view_system();
    sh::game::SystemBlock& sys = session().system;
    if (change_debug_mode) {
        s_driver.mode++;
    }
    cam::OrbitInput orbit_in;
    orbit_in.closer = for_f != 0;
    orbit_in.farther = back_f != 0;
    orbit_in.turn_right = right_f != 0;
    orbit_in.turn_left = left_f != 0;
    orbit_in.up = up_f != 0;
    orbit_in.down = down_f != 0;

    switch (s_driver.mode) {
        default: {
            // Restart: the camera is placed beside Harry, then follows him like mode 0.
            s_driver.mode = kFollow;
            const cam::HarryView harry = host.harry();
            cam::Vec3 start{harry.position.x + config.debug.start_offset_x, config.debug.start_y, harry.position.z};
            vcSetFirstCamWork(&start, harry.yaw, 0);
        }
            [[fallthrough]];
        case kFollow: {
            const cam::HarryView harry = host.harry();
            cam::SubjectSource src;
            src.position = harry.position;
            src.connected = in_connect_f != 0;
            if (!src.connected) {
                src.ground_height = host.ground_at(harry.position.x, harry.position.z).height;
                cam::Vec3 head;
                vcMakeHeroHeadPos(&head);
                src.head = head;
            }
            src.elevation_rate = s_driver.elevation_rate;
            src.move_speed = harry.move_speed;
            src.heading = harry.heading;
            src.turn_speed = harry.turn_speed;
            src.yaw = harry.yaw;
            const cam::SubjectFrame f = cam::subject_of(src, config.subject);

            if (f.inhibit_far_look) {
                vcWorkSetFlags(cam::camera_flags::inhibit_far_look, 0);
            } else {
                vcWorkSetFlags(0, cam::camera_flags::inhibit_far_look);
            }
            cam::Vec3 pos = harry.position;
            cam::Vec3 head = f.subject.head;
            vcSetSubjChara(&pos, f.subject.bottom_y, f.subject.top_y, f.subject.ground_y, &head,
                           static_cast<std::int16_t>(f.subject.move_speed), f.subject.move_angle_y,
                           static_cast<std::int16_t>(f.subject.angle_speed_y),
                           static_cast<std::int16_t>(f.subject.eye_angle_y),
                           static_cast<std::int16_t>(f.subject.eye_angle_wy), f.subject.watch_xz_radius);
            s_driver.smooth_move = static_cast<std::uint8_t>(vcExecCamera());
            break;
        }
        case kOrbitReference: {
            s_driver.reference = orbit_reference(orbit_in);
            const cam::Vec3& ref = s_driver.reference;
            view.set_frame(nullptr, cam::orbit_matrix(ref, sys.camera_angle_y, sys.camera_angle_z, sys.camera_height,
                                                      sys.camera_radius_xz));
            break;
        }
        case kFly:
            fly_camera();
            break;
        case kOrbitHead: {
            s_driver.reference = orbit_reference(orbit_in);
            const cam::DebugCameraRules& d = config.debug;
            view.set_frame(host.harry_head(), cam::orbit_matrix(d.head_reference, d.head_orbit_yaw, d.head_orbit_roll,
                                                                d.head_orbit_height, d.head_orbit_radius));
            break;
        }
    }

    view.update();
}

std::int8_t Vc_StereoBalanceGet(const cam::Vec3* soundPos) {
    // Mono output: centred.
    if (session().game.settings.mono) {
        return 0;
    }
    cam::ViewSystem& view = cam::view_system();
    const cam::Vec3 d = cam::listener_direction(view.position(), *soundPos);
    const cam::Matrix listener = cam::frame_world(view.frame());
    const std::int32_t dir[3] = {d.x, d.y, d.z};
    return static_cast<std::int8_t>(sh::audio::stereo_placement(listener.m, dir).balance);
}

// One-line dump of everything that differs per camera shot (console CAMSNAP), for comparing a good
// and a bad framing of the same scene.
void Pc_CamSnapDump(void) {
    const cam::CameraState& st = camera_state();
    const cam::CameraRoad rd = current_road();
    const cam::MapPlace place = cam::camera_host().map_place();
    const std::string map(place.map);

    SH_DBG_ECHO("[CAMSNAP] map=%s room=%d camMv=%d rdFlags=0x%02X rdArea=%d rdCamMv=%d vcFlags=0x%08X", map.c_str(),
                place.room, s_last_move_type, (int)rd.flags, (int)rd.area_size, (int)rd.camera_move,
                (unsigned)st.flags);
    SH_DBG_ECHO("[CAMSNAP] camPos=(%ld,%ld,%ld) tgtPos=(%ld,%ld,%ld) watch=(%ld,%ld,%ld)", (long)st.cam_pos.x,
                (long)st.cam_pos.y, (long)st.cam_pos.z, (long)st.cam_target_pos.x, (long)st.cam_target_pos.y,
                (long)st.cam_target_pos.z, (long)st.watch_target_pos.x, (long)st.watch_target_pos.y,
                (long)st.watch_target_pos.z);
    const cam::ScreenProjection proj = cam::projection_now();
    SH_DBG_ECHO("[CAMSNAP] matAng=(%d,%d,%d) geomH=%d gsH=%d gteOfs=(%ld,%ld)", (int)st.cam_rotation.x,
                (int)st.cam_rotation.y, (int)st.cam_rotation.z, (int)st.geom_screen_dist,
                (int)session().game.screen_height, (long)(proj.ofx >> 16), (long)(proj.ofy >> 16));
    {
        // Project (watch - cam) with the camera matrix through the engine projection, to compare
        // against the same numbers computed offline from the printed inputs.
        cam::Matrix m;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                m.m[i][j] = st.cam_matrix.m[j][i];
            }
        }
        const cam::Vec3 d = {(std::int16_t)(st.watch_target_pos.x - st.cam_pos.x),
                             (std::int16_t)(st.watch_target_pos.y - st.cam_pos.y),
                             (std::int16_t)(st.watch_target_pos.z - st.cam_pos.z)};
        cam::ScreenProjection p = proj;
        p.whole_map_far = false;
        std::int16_t sx = 0, sy = 0;
        const std::int32_t otz = cam::project_depth(m, p, d, sx, sy);
        SH_DBG_ECHO("[CAMSNAP] gteProbe d=(%d,%d,%d) -> scr=(%d,%d) otz=%ld", (int)d.x, (int)d.y, (int)d.z, (int)sx,
                    (int)sy, (long)otz);
    }
    SH_DBG_ECHO("[CAMSNAP] camMat=[%d,%d,%d / %d,%d,%d / %d,%d,%d] t=(%ld,%ld,%ld)", (int)st.cam_matrix.m[0][0],
                (int)st.cam_matrix.m[0][1], (int)st.cam_matrix.m[0][2], (int)st.cam_matrix.m[1][0],
                (int)st.cam_matrix.m[1][1], (int)st.cam_matrix.m[1][2], (int)st.cam_matrix.m[2][0],
                (int)st.cam_matrix.m[2][1], (int)st.cam_matrix.m[2][2], (long)st.cam_matrix.t[0],
                (long)st.cam_matrix.t[1], (long)st.cam_matrix.t[2]);
    // The final render matrix: rotation transpose(cam_mat)*diag(1,0.75,1), t that matrix applied to
    // -campos (Q8); any deviation is the vertical offset, quantified.
    const cam::Matrix& ws = cam::camera_host().world_screen();
    SH_DBG_ECHO("[CAMSNAP] wsMat=[%d,%d,%d / %d,%d,%d / %d,%d,%d] t=(%ld,%ld,%ld)", (int)ws.m[0][0], (int)ws.m[0][1],
                (int)ws.m[0][2], (int)ws.m[1][0], (int)ws.m[1][1], (int)ws.m[1][2], (int)ws.m[2][0], (int)ws.m[2][1],
                (int)ws.m[2][2], (long)ws.t[0], (long)ws.t[1], (long)ws.t[2]);
}

}  // extern "C"
