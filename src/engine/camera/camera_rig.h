#pragma once

// The camera rig: one map's zone list plus the persistent camera state, and stage A of the frame
// update — everything the original view camera decided up to and including the zone warp flag: NPC
// death timers, view-button bookkeeping, screen half angles, the through-door rail, the nearest
// enemy, near-zone gathering and current-zone selection, the move type, the far-look and
// self-view rates, and the flags derived from the move type.
//
// Bit-exactness contract: with identical inputs (state + FrameInput + zone list) update_zone
// produces the original's results to the last bit. The order of operations matches the original frame
// sequence, because later steps read what earlier ones wrote (the death timers feed the enemy
// search, the enemy feeds the gathering, the gathering feeds the selection).

#include <cstddef>
#include <vector>

#include "engine/camera/camera_types.h"

namespace sh::camera {

// The engine's fallback camera zone: used as the current zone whenever no zone of the map
// matches, and as the WHOLE zone list when the game runs a map without one. Field values are the
// shipped default's stored bits (its XZ bounds are +-7680 in Q4, i.e. +-480 m).
CameraRoad default_camera_road();

// The fallback zone as a near-list entry: kDefaultRoad, zero distance and offsets, the fallback
// zone's areas. What the camera holds as its current zone before any zone of a map engaged.
NearRoad fallback_near_road();

// What the camera knows of its subject each frame (camera_api.cpp, CameraRig::set_subject). All
// Q12; angles are Q12 angles.
struct Subject {
    Vec3 position{};
    std::int32_t bottom_y = 0;
    std::int32_t top_y = 0;
    std::int32_t ground_y = 0;
    Vec3 head{};
    std::int32_t move_speed = 0;
    std::int32_t move_angle_y = 0;
    std::int32_t angle_speed_y = 0;
    std::int32_t eye_angle_y = 0;
    std::int32_t eye_angle_wy = 0;
    std::int32_t watch_xz_radius = 0;
};

class CameraRig {
public:
    // Installs the zone list. The list is cut at the first entry carrying road_flags::end_marker
    // (the marker itself is dropped): the original iterated a data array that the end marker
    // terminated. roads_are_default must be true exactly when the game runs with no map zone
    // list: then list entry 0 and the fallback zone are the SAME zone (the original compared road
    // pointers and the fallback list's entry 0 was the fallback), so resolve_road() maps
    // kDefaultRoad to 0 and back, keeping stored references and comparisons equal, like the original's
    // pointer equality did.
    void set_roads(std::vector<CameraRoad> roads, bool roads_are_default);

    CameraState& state() { return state_; }
    const CameraState& state() const { return state_; }

    bool roads_are_default() const { return roads_are_default_; }

    // The zone a reference names; kDefaultRoad names the fallback zone.
    const CameraRoad& road(RoadRef ref) const;

    // Number of zones in the installed list (after cutting at the end marker).
    std::size_t road_count() const { return roads_.size(); }

    // kDefaultRoad resolved against the list (see set_roads); every other reference is itself.
    RoadRef resolve_road(RoadRef ref) const;

    // Stage A of the frame update. See camera_types.h for the result.
    StageAResult update_zone(const FrameInput& input);

    // Stage B, after stage A of the same frame: the motion parameters, the camera target (unless a
    // script owns the camera position) and the camera position following it.
    StageBResult update_target(const FrameInput& input, const StageAResult& stage_a);

    // Stage C, after stage B of the same frame: the look-at target, the camera rotation and matrix,
    // the view handed to the view system and the end-of-frame bookkeeping. `frame_start_pos` and
    // `frame_start_rotation` are the camera position and rotation before stage A.
    StageCResult update_view(const FrameInput& input, const StageAResult& stage_a, const StageBResult& stage_b,
                             const Vec3& frame_start_pos, const Angles& frame_start_rotation);

    // A, B and C in order.
    FrameResult update(const FrameInput& input);

    // ---- the camera API scripts and the game use between frames (camera_api.cpp) ----

    // A map begins: the camera is off, the current zone is the fallback zone, there is no previous
    // exclusion radius, the look-at may rise up to `watch_max_y`, no look-at update is pending.
    void init_system(std::int32_t watch_max_y);
    // The camera runs from the next frame on, projecting at `screen_distance`.
    void start_system(std::int16_t screen_distance);
    void stop_system() { state_.view_active = false; }

    // Places the camera at `pos` at rest, snapping camera, look-at and target on the next frame,
    // facing the way the subject looks (`eye_angle_y`); `through_door` engages the door rail.
    void set_first_camera(const Vec3& pos, std::int16_t eye_angle_y, bool through_door);

    // flags = (flags | enable) & ~disable (camera_flags bits).
    void set_flags(std::uint32_t enable, std::uint32_t disable);

    // Sets how far the look-at may rise; returns the previous limit.
    std::int32_t set_watch_max_y(std::int32_t max_y);

    // A script takes the look-at: the look-at goes to `target` with `params`, snapping when `warp`.
    // The script look-at also drops the "character visible" bit.
    void user_watch_target(const Vec3& target, const WatchMoveParams& params, bool warp);
    // A script takes the camera position: the camera travels to `target` with `params`.
    void user_camera_target(const Vec3& target, const CamMoveParams& params, bool warp);
    // The automatic camera takes over again (snapping everything when `warp`); the projection is
    // reset to `screen_distance` unless `keep_projection`.
    void return_to_auto_camera(bool warp, bool keep_projection, std::int16_t screen_distance);

    void set_projection(std::int16_t screen_distance) { state_.geom_screen_dist = screen_distance; }
    void request_look_at_point_update() { state_.update_look_at_point = true; }
    void set_watch_position_y(std::int32_t y) { state_.watch_position_y = y; }
    // A look-at matrix attached to a frame of the world (already composed by the caller).
    void set_look_at_matrix(const Matrix& m);

    // The subject of the camera for the next frame.
    void set_subject(const Subject& subject);

private:
    std::vector<CameraRoad> roads_;
    bool roads_are_default_ = false;
    CameraState state_;
};

}  // namespace sh::camera
