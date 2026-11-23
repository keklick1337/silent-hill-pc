#pragma once

// Alternate cameras of the PC control styles: third person (an
// orbit behind Harry), over the shoulder (the orbit shifted to one side) and first person (an eye
// riding Harry's head). The mouse and the right stick turn the view independently of Harry's body;
// aiming eases the orbit in; walls pull the third-person eye in toward Harry's head; in first
// person the eye follows the head bone's sway and look, and backs away from the arms in a melee
// swing.
//
// Every distance, angle, speed and ease comes from the tuning (gamedata/config/alt_cameras.json,
// replaceable by mods); the player's options (sensitivity, zoom, shoulder offsets) come with the
// frame input. The host answers the level ray queries. Units are the world's: Q12 positions,
// Q12 turns (4096 a turn), +Y down.
//
// First person is integer arithmetic throughout. Third person and
// over the shoulder are a spring arm: the pivot follows Harry through a critically damped spring,
// zoom and shoulder offset ease the same way, the look input may be smoothed, a cone of rays keeps
// the eye out of walls (in at once, back out eased) and the pitch levels itself while Harry moves.
// Every ease is a closed form of the frame time, so the feel does not depend on the frame rate.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "formats/common/status.h"

namespace sh::camera {

struct AltVec3 {
    std::int32_t x = 0, y = 0, z = 0;
    friend bool operator==(const AltVec3&, const AltVec3&) = default;
};

// The tuning, as the data file states it (units and degrees); converted once like the game's
// constants (single precision, truncated).
struct AltCameraTuning {
    float orbit_distance = 0.0f;       // rest radius of the orbit
    float aim_distance = 0.0f;         // radius while aiming at the default (100%) zoom
    float orbit_height = 0.0f;         // eye lift above Harry's root (negative = up)
    float look_at_height = 0.0f;       // look target height above the root (chest)
    float head_height = 0.0f;          // Harry's head above the root: the wall pull pivot
    float look_at_distance = 0.0f;     // look target this far ahead along the view
    int mouse_yaw = 0;                 // angle units per mouse pixel
    int mouse_pitch = 0;
    int stick_dead_zone = 0;           // of 128
    int stick_yaw = 0;                 // angle units per 30 fps frame at full deflection
    int stick_pitch = 0;
    float pitch_min_deg = 0.0f;        // third person pitch limits
    float pitch_max_deg = 0.0f;
    float fps_pitch_limit_deg = 0.0f;  // first person look up/down limit
    float fps_yaw_limit_deg = 0.0f;    // first person look left/right of the body
    float fps_view_pitch_limit_deg = 0.0f;  // with the head look added
    int zoom_percent_limit = 0;        // the zoom options' range, +-
    int head_follow_shift = 0;         // first person head reference low-pass: 1/2^n a frame
    std::uint32_t head_look_delay_ms = 0;  // head look off for this long after Harry stops
    std::uint32_t head_look_full_ms = 0;   // ... and fully on from here
    float head_sway_limit = 0.0f;      // first person: the eye leaves the tuned spot by no more than this
    float swing_near = 0.0f;           // arm distance where the melee dolly starts
    float swing_wall = 0.0f;           // keep-out margin from level geometry behind the eye
    float swing_head_show = 0.0f;      // dolly distance above which the head is drawn
    float swing_head_hide = 0.0f;      // ... and below which it is hidden again
    float swing_snap = 0.0f;           // the dolly snaps home within this distance
    float wall_margin = 0.0f;          // third person eye stays this far from a wall
    float wall_min = 0.0f;             // ... and never closer to the head than this
    // Third person spring arm (times are smooth times in seconds: about the time to close the gap).
    float follow_time = 0.0f;          // pivot follow at 100% camera smoothing (scaled by the option)
    float follow_vertical_scale = 1.0f;  // vertical follow is this much softer (stairs, slopes)
    float follow_aim_scale = 1.0f;     // ... and this much tighter while aiming
    float max_lag = 0.0f;              // the pivot never trails Harry by more than this (units)
    float teleport_distance = 0.0f;    // a jump farther than this snaps the camera
    float zoom_time = 0.0f;            // orbit radius and shoulder offset
    float probe_radius = 0.0f;         // half size of the ray cone at the eye
    float ease_out_time = 0.0f;        // back out after a wall let go
    float close_lift = 0.0f;           // the eye rises this much when a wall presses it fully in
    float close_shoulder = 1.0f;       // ... and keeps this share of the shoulder offset
    float look_smoothing_time = 0.0f;  // look input at 100% look smoothing
    float stick_exponent = 1.0f;       // look stick response curve (1 = linear)
    float recenter_time = 0.0f;        // pitch levelling
    float recenter_pitch_deg = 0.0f;   // ... toward this pitch
};

Result<AltCameraTuning> parse_alt_camera_tuning(std::string_view json_text);

// gamedata/config/alt_cameras.json, or the last mod that replaces it; `source` gets the file used.
Result<AltCameraTuning> load_alt_camera_tuning(const std::filesystem::path& gamedata_dir,
                                               const std::filesystem::path& mods_dir, std::string* source);

enum class AltStyle { ThirdPerson, OverShoulder, FirstPerson };

// The player's options for the frame, already resolved for the active style.
struct AltCameraOptions {
    float mouse_sensitivity = 1.0f;
    float pad_sensitivity = 1.0f;
    bool invert_mouse_y = false;
    bool invert_pad_y = false;
    float aim_zoom_percent = 100.0f;   // -200..200: 0 = no zoom, 100 = the aim distance, 200 = twice as far in
    float rest_zoom_percent = 0.0f;    // the resting distance on the same scale
    std::int32_t rest_x = 0, rest_y = 0;  // shoulder offset at rest (Q12, X along the right vector)
    std::int32_t aim_x = 0, aim_y = 0;    // ... and while aiming
    bool wall_collision = true;        // false: the render eye may pass through walls (the aim ray never does)
    float follow_smoothing = 0.5f;     // 0..1: third person pivot lag (0 = rigid)
    float look_smoothing = 0.0f;       // 0..1: third person look input smoothing (0 = raw)
    bool recenter = true;              // third person: level the pitch while Harry moves
    float recenter_delay = 2.0f;       // ... after this long without look input (seconds)
    bool head_tracking = true;         // first person: the view turns with the head bone
    float melee_swing = 0.0f;          // first person dolly cap, units (0 = off)
    AltVec3 eye_offset;                // first person eye in Harry's body frame (x right, y up negative, z forward)
};

// A bone in world space: translation in Q8 (as the skeleton stores it) and, for the head, the
// world direction of its local +Z axis (Q12).
struct AltBone {
    std::int32_t tx = 0, ty = 0, tz = 0;
    std::int32_t fx = 0, fy = 0, fz = 0;
};

struct AltCameraInput {
    AltStyle style = AltStyle::ThirdPerson;
    // Entering from the classic camera, the end of a scripted scene, a load or a warp: the view
    // snaps behind Harry (first person: to his eyes) looking where he faces, zoom cleared.
    bool reset = false;
    std::int32_t yaw = 0, pitch = 0;   // the orbit angles as the game holds them (other code turns them too)
    AltVec3 harry;                     // root position
    std::int32_t harry_yaw = 0;
    AltBone head;
    AltBone arms[4];                   // left forearm, left hand, right forearm, right hand
    bool aiming = false;
    bool melee_attack = false;         // a melee or unarmed attack is under way
    bool rear_look = false;
    int shoulder_side = 1;             // +1 right, -1 left
    bool frozen = false;               // console or quick options open: the view holds still
    int mouse_dx = 0, mouse_dy = 0;
    int right_x = 128, right_y = 128, left_x = 128, left_y = 128;  // raw sticks
    bool moving = false;               // walking input this frame (first person head look delay, third person recenter)
    bool in_gameplay = false;          // settled gameplay: the head references may be taken
    std::uint64_t now_ms = 0;
    std::int32_t delta_time = 0;       // Q12 seconds
    AltCameraOptions options;
};

struct AltCameraOutput {
    std::int32_t yaw = 0, pitch = 0;
    AltVec3 eye, look_at;              // the view
    AltVec3 aim_origin, aim_forward;   // the free-aim ray: from the eye through the screen centre
    int aim_device = -1;               // -1 unchanged, 0 mouse, 1 pad
    // First person only (valid when first_person is true).
    bool first_person = false;
    AltVec3 view_forward;              // for the head-mounted flashlight
    bool head_visible = false;         // the melee dolly is far enough back to draw the head
    AltVec3 head_reference, head_local;  // the head sway reference and the head now (tuning readout)
};

// The level as the camera asks it: a ray from `from` to `to`; true and the distance of the first hit.
class AltCameraHost {
public:
    virtual ~AltCameraHost() = default;
    virtual bool ray_hit(const AltVec3& from, const AltVec3& to, std::int32_t* distance) = 0;
};

class AltCamera {
public:
    explicit AltCamera(const AltCameraTuning& tuning);

    AltCameraOutput update(const AltCameraInput& in, AltCameraHost& host);

    // The projection distance of a horizontal field of view on the 320-wide frame (rounded to
    // nearest, so the default 71.1 degrees gives the game's own 224), clamped to 16..512;
    // `fallback` for a degenerate angle.
    static std::int32_t projection_distance(float fov_deg, std::int32_t fallback);

private:
    struct Tuned;  // the tuning in Q12 and angle units
    void look(const AltCameraInput& in, AltCameraOutput& out, std::int32_t& yaw, std::int32_t& pitch);

    void look_orbit(const AltCameraInput& in, AltCameraOutput& out, std::int32_t& yaw, std::int32_t& pitch, double dt);
    void orbit(const AltCameraInput& in, AltCameraOutput& out, std::int32_t yaw, std::int32_t pitch, double dt,
               AltCameraHost& host);

    std::int32_t q(float units) const;
    std::int32_t deg(float degrees) const;

    // A value eased by a critically damped spring toward a target that moves in a straight line
    // over each frame.
    struct Spring {
        double x = 0.0, v = 0.0, target = 0.0;
        void snap(double to) { x = target = to; v = 0.0; }
        void step(double to, double smooth_time, double dt);
    };

    AltCameraTuning t_;
    // Third person spring arm.
    bool arm_valid_ = false;
    Spring follow_[3];                 // the pivot (Harry's root, lagged), Q12 units
    Spring distance_;                  // orbit radius
    Spring shoulder_x_, shoulder_y_;
    double arm_frac_ = 1.0;            // the wall-limited share of the arm in use
    double pending_yaw_ = 0.0, pending_pitch_ = 0.0;  // smoothed look input not applied yet
    double carry_yaw_ = 0.0, carry_pitch_ = 0.0;      // fractions of an angle unit
    double look_idle_ = 0.0;           // seconds since the view was last turned
    double recenter_carry_ = 0.0;
    // First person head follow.
    AltVec3 head_ref_{};
    bool head_ref_valid_ = false;
    std::int16_t head_yaw_ref_ = 0, head_pitch_ref_ = 0;
    bool head_rot_valid_ = false;
    std::uint32_t last_move_ms_ = 0;
    std::int32_t swing_pull_ = 0;
    bool head_visible_ = false;
};

}  // namespace sh::camera
