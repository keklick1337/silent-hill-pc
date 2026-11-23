#pragma once

// The camera module's data types. Everything is neutral engine data: the game-side bridge
// (camera_entry.cpp, over camera_host.h) turns the game's characters, maps and options into
// these inputs and the camera zone records of the map data into CameraRoad values; nothing here
// knows character or map identities.
//
// Bit-exactness: the camera decides replay hashes, so the module reproduces the original view-camera
// code's integer arithmetic to the last bit — same Q formats, same truncating divisions, same
// 32-bit wraparound, same >= / < tie rules. Where a rule looks odd it is the original's rule and
// is kept deliberately.

#include <cstdint>
#include <functional>
#include <vector>

namespace sh::camera {

// --- value types (the original's Q formats, as plain integers) ---

struct Vec3 {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

// Q.12 angles: 4096 is one full turn, stored 16-bit like the original's angle fields.
struct Angles {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

struct Matrix {
    std::int16_t m[3][3] = {};
    std::int32_t t[3] = {};
};

// Axis-aligned XZ rectangle in Q.4 (16 units = 1 m), exactly as stored in map data.
struct Area {
    std::int16_t min_x = 0;
    std::int16_t max_x = 0;
    std::int16_t min_z = 0;
    std::int16_t max_z = 0;
};

// --- camera zones ("roads" in the map data) ---

// Flag bits of a zone. The values are the map data's flag byte, so they are an interface, not a
// free choice.
namespace road_flags {
constexpr std::uint8_t end_marker        = 1 << 0;  // terminates the zone list
constexpr std::uint8_t warp_in           = 1 << 1;  // snap the camera when this zone engages
constexpr std::uint8_t warp_out          = 1 << 2;  // snap the camera when this zone is left
constexpr std::uint8_t no_front_flip     = 1 << 3;  // chase cam never flips to face the character
constexpr std::uint8_t limit_far_look_up = 1 << 4;  // clamp the look-at Y while far-looking
constexpr std::uint8_t use_without_enemy = 1 << 5;  // zone only engages with NO enemy near
constexpr std::uint8_t use_with_enemy    = 1 << 6;  // zone only engages WITH an enemy near
constexpr std::uint8_t merge_road        = 1 << 7;  // distance may go negative (zones merge)
}  // namespace road_flags

enum class AreaSize : std::uint8_t { Tiny = 0, Small = 1, Wide = 2, Outdoor = 3 };

enum class RoadType : std::uint8_t {
    Road = 0,
    Effect = 1,
    Event = 2,
    LowPriorityRoad = 3,   // data also carries these; the selection treats them as Road
    HighPriorityRoad = 4,
    SelfViewOnly = 5,
};

// The camera_move field of the data is a signed 4-bit value; 0..4 are the known moves.
enum class CameraMove : std::int8_t {
    Chase = 0,
    Settle = 1,
    FixedAngle = 2,
    SelfView = 3,
    ThroughDoor = 4,
};

// One camera zone of a map, decoded. Field names follow the keys of
// gamedata/maps/<map>/cameras.json.
struct CameraRoad {
    Area switch_area{};                    // where the character must be for the zone to engage
    Area road_area{};                      // where the camera may travel
    std::uint8_t flags = 0;                // road_flags bits
    AreaSize area_size = AreaSize::Tiny;
    RoadType road_type = RoadType::Road;
    std::uint8_t move_y_type = 0;          // a CameraMove value governing the Y response
    std::int16_t road_max_y = 0;           // Q4
    std::int16_t road_min_y = 0;           // Q4
    std::int16_t watch_offset_y = 0;       // Q4
    std::uint8_t lens_flare = 0;
    CameraMove camera_move = CameraMove::Chase;
    std::int8_t fix_angle_x = 0;           // Q.8 angle (256 = full turn)
    std::int8_t fix_angle_y = 0;
};

// Reference to a zone of the rig's list. kDefaultRoad names the engine's fallback zone (below),
// used when no zone of the map matches.
using RoadRef = std::int32_t;
inline constexpr RoadRef kDefaultRoad = -1;

// One zone of the current frame's near list: the zone plus the character-relative geometry the
// selection computed for it.
struct NearRoad {
    RoadRef road = kDefaultRoad;
    std::uint8_t axis = 0;        // 0: zone runs along Z, 1: along X (the wider extent)
    std::uint8_t priority = 0;    // by road_type: Event 2, Effect 1, everything else 0
    std::int32_t distance = 0;    // Q12, character to the zone's switch box (may be negative)
    std::int32_t offset_x = 0;    // Q12, signed offset to the switch box on X
    std::int32_t offset_z = 0;    // Q12, signed offset to the switch box on Z
    Area road_area{};
    Area switch_area{};
};

// The through-door rail camera.
struct ThroughDoorRail {
    bool active = false;
    std::int32_t timer = 0;     // Q12 seconds since the rail engaged
    std::int16_t rail_yaw = 0;  // Q12 angle
    Vec3 start{};               // Q12, where the character stood when the rail engaged
    std::int32_t distance = 0;  // Q12, rail start to character on XZ
};

// Flag bits of the camera state. The values are set and read by game scripts through the bridge,
// so they are an interface.
namespace camera_flags {
constexpr std::uint32_t user_camera           = 1 << 0;   // a script owns the camera position
constexpr std::uint32_t user_watch            = 1 << 1;   // a script owns the look-at
constexpr std::uint32_t warp_camera           = 1 << 2;   // teleport the camera this frame
constexpr std::uint32_t warp_watch            = 1 << 3;   // teleport the look-at this frame
constexpr std::uint32_t warp_camera_target    = 1 << 4;   // teleport the camera target this frame
constexpr std::uint32_t switch_near_road      = 1 << 5;
constexpr std::uint32_t projection_change     = 1 << 6;
constexpr std::uint32_t character_visible     = 1 << 7;
constexpr std::uint32_t inhibit_far_look      = 1 << 8;
constexpr std::uint32_t view_button           = 1 << 9;   // the view button is held this frame
constexpr std::uint32_t view_button_previous  = 1 << 10;  // ... and was held last frame
}  // namespace camera_flags

// Look-at motion parameters (Q12).
struct WatchMoveParams {
    std::int32_t accel_x = 0;
    std::int32_t accel_y = 0;
    std::int16_t max_speed_x = 0;
    std::int16_t max_speed_y = 0;
};

// Camera motion parameters (Q12).
struct CamMoveParams {
    std::int32_t accel_xz = 0;
    std::int32_t accel_y = 0;
    std::int32_t max_speed_xz = 0;
    std::int32_t max_speed_y = 0;
};

// The whole persistent camera state.
inline constexpr int kNearRoadCapacity = 10;  // the original's fixed near-list size

struct CameraState {
    bool view_active = false;
    std::uint32_t flags = 0;  // camera_flags bits
    bool through_door_activate = false;  // engage the rail camera on the next update
    ThroughDoorRail through_door{};
    std::int16_t screen_half_angle_x = 0;  // Q12, half of the horizontal view angle
    std::int16_t screen_half_angle_y = 0;  // Q12, half of the vertical view angle
    std::int16_t geom_screen_dist = 0;     // the projection distance the screen angles derive from
    CamMoveParams user_cam_params{};
    Vec3 cam_target_pos{};
    Vec3 cam_pos{};
    std::int16_t cam_move_angle_y = 0;
    Vec3 cam_velocity{};
    std::int32_t old_cam_exclusion_radius = -1;  // Q12; -1 = no previous radius
    WatchMoveParams user_watch_params{};
    Vec3 watch_target_pos{};
    std::int32_t watch_target_max_y = 0;
    std::int16_t watch_target_roll = 0;
    Angles cam_rotation{};
    Matrix cam_matrix{};
    Angles cam_rotation_offset{};
    Angles cam_rotation_offset_speed{};
    Angles base_cam_rotation{};
    std::int8_t unknown_d0[8] = {};  // carried for the state round-trip; purpose never identified
    bool update_look_at_point = false;
    Matrix look_at_matrix{};
    bool update_look_at_matrix = false;
    std::int16_t cam_to_ideal_angle_y = 0;
    Vec3 cam_target_velocity{};
    std::int16_t cam_target_move_angle_y = 0;
    std::int32_t cam_target_speed = 0;
    Vec3 chara_pos{};
    std::int32_t chara_bottom_y = 0;
    std::int32_t chara_top_y = 0;
    std::int32_t chara_center_y = 0;
    std::int32_t chara_ground_y = 0;
    Vec3 chara_head_pos{};
    std::int32_t chara_move_speed = 0;
    std::int16_t chara_move_angle_y = 0;
    std::int16_t chara_angle_speed_y = 0;
    std::int16_t chara_eye_angle_y = 0;
    std::int16_t chara_eye_angle_wy = 0;
    std::int32_t chara_watch_xz_radius = 0;
    NearRoad near_roads[kNearRoadCapacity]{};
    std::int32_t near_road_count = 0;
    NearRoad current_road{};
    std::int32_t nearest_enemy = -1;  // NPC slot index, -1 = none
    std::int32_t nearest_enemy_xz_distance = 0;
    std::int32_t watch_position_y = 0;
    // Look-at motion parameters blended between the default and the self-view set; persists across
    // frames (only rewritten while no script owns the look-at).
    WatchMoveParams blended_watch_params{};
    std::int32_t self_view_timer = 0;  // Q12 seconds, drives the self-view head-bob noise
};

// --- per-frame inputs, filled by the bridge ---

// One NPC as the camera sees it. The classification (which characters count as enemies, which
// stance flags they carry) is game knowledge and stays in the bridge.
struct NpcView {
    bool present = false;
    bool camera_enemy = false;   // a character the camera logic tracks
    bool always_counts = false;  // enemy kind that is always an active camera target
    bool excluded = false;       // flagged invisible to the camera
    bool passive = false;        // idle stance: counts only while the player aims at it
    bool is_target = false;      // the player's current target
    std::int32_t health = 0;
    std::int32_t dead_time = 0;  // Q12 seconds dead, BEFORE this frame's update
    Vec3 position{};
    // Where the far look-at aims when this NPC is the nearest enemy: its own height, the ground
    // under it, or the camera's look-at height; plus a per-character height offset (Q12).
    enum class LookAnchor : std::uint8_t { Character = 0, Ground = 1, Camera = 2 };
    LookAnchor look_anchor = LookAnchor::Character;
    std::int32_t look_offset_y = 0;
};

// The ground under a point, as the world reports it.
struct GroundSample {
    bool found = false;        // false: no walkable ground there
    std::int32_t height = 0;   // Q12
};

// What the self-view look-at rule gets from the camera: the pitch toward the current look-at
// (clamped to +-70 degrees) and whether an enemy is near.
struct GazeQuery {
    std::int32_t pitch = 0;
    bool enemy_near = false;
};

// A widescreen correction of one authored shot: when the camera is within `tolerance` of the
// anchor, the look-at is swung about the camera by `yaw_delta`.
struct WideShot {
    bool enabled = false;
    std::int32_t anchor_x = 0;
    std::int32_t anchor_z = 0;
    std::int32_t tolerance = 0;
    std::int32_t yaw_delta = 0;
};

// A per-map anchor correction: the X of one zone's road area is shifted while gathering (a
// shipped-camera fix the bridge resolves by map and room; the engine only applies the shift).
struct RoadAnchorShift {
    RoadRef road = kDefaultRoad;
    std::int32_t x_shift = 0;  // Q12, added to both X bounds of the road area
};

// Motion tuning of the automatic camera, the same for every map unless a script changes it.
struct CameraTuning {
    WatchMoveParams follow_watch{4915, 9011, 901, 2867};      // look-at motion of the follow camera
    WatchMoveParams self_view_watch{6881, 12615, 1802, 5734}; // look-at motion of the self view
    CamMoveParams follow_camera{40960, 12288, 24576, 8192};   // camera motion of the follow camera
};

struct FrameInput {
    std::int32_t delta_time = 0;  // Q12 seconds
    CameraTuning tuning{};
    bool view_button_held = false;
    bool extra_view_mode = false;   // the "self view" option
    bool extra_view_ctrl = false;   // the "reverse" view-button option
    std::int32_t screen_width = 0;
    std::int32_t screen_height = 0;
    bool combat_suppresses_enemies = false;  // a scripted fight scene: no nearest enemy
    bool player_aiming = false;
    std::vector<NpcView> npcs;  // one entry per NPC slot, in slot order
    bool self_view_blocked = false;  // map-specific zones where self view must not engage
    bool tight_doorways = false;     // map family with narrow doorways (rail camera tuning)
    std::vector<RoadAnchorShift> road_anchor_shifts;
    // The ground under a point (used for an enemy whose look anchor is the ground).
    std::function<GroundSample(std::int32_t x, std::int32_t z)> ground_at;
    // The subject's self-view gaze angles (Q12 pitch, yaw, roll) before the camera's own clamp:
    // how the head of the subject character looks is game knowledge.
    std::function<Angles(const GazeQuery&)> self_view_gaze;
    WideShot wide_shot{};
};

// What stage A computed for the frame.
struct StageAResult {
    bool warp = false;                  // the camera must snap this frame (zone warp in/out)
    std::int32_t move_type = 0;         // CameraMove value
    std::int32_t far_look_rate = 0;     // Q12 blend, 0..4096
    std::int32_t self_view_rate = 0;    // Q8 strength, 0..4096
    std::vector<std::int32_t> npc_dead_time;  // parallel to FrameInput::npcs (present entries)
};

// What stage B used for the frame: the look-at and camera motion parameters in effect.
struct StageBResult {
    WatchMoveParams watch_params{};
    CamMoveParams cam_params{};
};

// What the camera hands to the view system this frame.
struct ViewOutput {
    enum class Kind : std::uint8_t {
        HeadLookAt = 0,    // view from the subject's head bone (the game's own view setup)
        DirectMatrix = 1,  // the camera matrix in `matrix`
    };
    Kind kind = Kind::DirectMatrix;
    Matrix matrix{};
};

struct StageCResult {
    ViewOutput view{};
    std::int32_t smooth_move_type = 0;  // 1 (settle) when the camera barely moved this frame, else 0
};

// The whole frame.
struct FrameResult {
    StageAResult zone{};
    StageBResult target{};
    StageCResult view{};
};

// Signed distance from a point to an Area on the XZ plane, in Q12 — the game's box distance,
// reproduced operation by operation (the branch structure and the negative-distance rules are
// load-bearing for the zone selection). The bounds are passed already converted to Q12.
// *out_x / *out_z receive the signed offsets to the box; with allow_negative the distance of a
// point inside the box is negative (how far past the near edge it stands, on the side of the
// centre it is on), which is how merged zones overlap.
std::int32_t area_distance(std::int32_t* out_x, std::int32_t* out_z,
                           std::int32_t x, std::int32_t z,
                           std::int32_t min_x, std::int32_t max_x,
                           std::int32_t min_z, std::int32_t max_z,
                           bool allow_negative);

}  // namespace sh::camera
