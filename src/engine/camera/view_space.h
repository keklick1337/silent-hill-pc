#pragma once

// The view space of the engine camera: the frame
// hierarchy walk that gives a frame its world matrix, the eye and the world-to-screen matrix made
// from it, the eye placements the game asks for (a reference view, a look-at, an orbit around a
// point), the pose read back from the eye, and the two "is this box on the screen" tests (the
// logic one with the console's exact 4:3 bounds, the draw one widened for the PC view).
//
// Everything here is pure integer arithmetic on engine types and returns the original game's
// numbers to the last bit
// (tests/camera/view_space_test.cpp). The frames themselves belong to the host: they are linked
// records the rest of the game keeps (bones, the view frame), so the walk reads and writes them
// through FrameHost.

#include <cstdint>

#include "engine/camera/camera_types.h"

namespace sh::camera {

// ---- frame hierarchy --------------------------------------------------------------------------

// The host's linked frames. A frame has a local matrix (relative to its parent), a world matrix
// cache and a "world is current" counter; the walk also threads a child link through the chain it
// climbs (the link stays behind afterwards).
class FrameHost {
public:
    using Frame = void*;

    virtual ~FrameHost() = default;

    // A frame pointer the host can follow at all (null and wild links are not).
    virtual bool usable(Frame frame) const = 0;
    // The parent, or null at the root. A link the host cannot follow is reported as null.
    virtual Frame parent(Frame frame) = 0;
    virtual bool world_current(Frame frame) const = 0;
    virtual void mark_world_current(Frame frame) = 0;
    virtual void set_child(Frame frame, Frame child) = 0;
    virtual Frame child(Frame frame) const = 0;
    virtual Matrix local(Frame frame) const = 0;
    virtual Matrix world(Frame frame) const = 0;
    virtual void set_world(Frame frame, const Matrix& world) = 0;
    // Called once per unusable root that is not null (diagnostics only).
    virtual void unusable_root(Frame) {}
};

// The world matrix of `frame`: every frame above it whose world is not current is recomputed from
// the topmost stale one down (parent world * local, the entity compose rules) and marked current.
// An unusable frame gives the identity.
Matrix frame_world(FrameHost& host, FrameHost::Frame frame);

// parent * local with the game's fixed-point rules (16-bit rotation, 32-bit translation).
Matrix compose(const Matrix& parent, const Matrix& local);

// ---- eye and world-screen matrix --------------------------------------------------------------

struct WorldScreen {
    Matrix eye;            // world matrix of the eye (rotation: view basis in columns; t: eye position)
    Matrix world_to_view;  // the rotation of `eye` transposed, no translation
    Matrix world_screen;   // world_to_view with the translation that moves the eye to the origin
};

WorldScreen world_screen_from_eye(const Matrix& eye_world);

// The local matrix of a reference view: the eye at `eye` looking at `target` (no roll), positions in
// geometry units.
Matrix reference_view_local(const Vec3& eye, const Vec3& target);

// A frame's matrix as seen from the eye: its world matrix moved by -eye and turned into view space.
Matrix view_matrix(const WorldScreen& ws, Matrix world);

// The world-to-screen matrix with the translation of the point `pos` (Q12) folded in.
Matrix world_screen_at(const Matrix& world_screen, const Vec3& pos);

// The eye at `pos` looking at `target` (Q12, no roll); the translation is in geometry units.
Matrix look_at_matrix(const Vec3& pos, const Vec3& target);

// The eye on a cylinder around `ref` (Q12): at `xz_radius` and `height` from it, on yaw `yaw`,
// rolled by `roll`, looking back at `ref`.
Matrix orbit_matrix(const Vec3& ref, std::int32_t yaw, std::int32_t roll, std::int32_t height,
                    std::int32_t xz_radius);

// Position (Q12) and Euler angles of a world matrix.
struct ViewPose {
    Vec3 position;
    Angles angles;
};
ViewPose pose_of(const Matrix& world);

// ---- camera workspace helpers ----------------------------------------------------------------

// Length of a Q12 vector, large components shifted down first (the camera's distance measure).
std::int32_t vector_length(std::int32_t x, std::int32_t y, std::int32_t z);

// The point the camera looks at now: as far ahead along its current rotation (pitch x, yaw y) as
// the look-at target is from the camera.
Vec3 now_watch_position(const Vec3& cam_pos, const Vec3& watch_target, const Angles& cam_rotation);

// A look-at matrix carried by a frame: the frame's world rotation turned by `rotation`, placed at
// `pos` (Q12; the translation is in geometry units).
Matrix attached_look_at(const Matrix& frame_world, const Angles& rotation, const Vec3& pos);

// ---- angle and segment helpers ----------------------------------------------------------------

// Narrows the arc [min, max] (Q12 angles, taken counter-clockwise from min) to its overlap with the
// arc [limit_min, limit_max]; an arc that does not overlap collapses onto its nearest end.
void clamp_angle_range(std::int16_t& min, std::int16_t& max, std::int16_t limit_min, std::int16_t limit_max);

// The fraction (Q12) of a segment where it crosses a bound line, or 1.0 when it does not cross it
// within [bounds_min, bounds_max]: `direction` is the segment's extent across the line,
// `distance` the distance from its start to the line, `length` its extent along the line.
std::int16_t segment_cross_fraction(std::int16_t length, std::int16_t direction, std::int16_t distance,
                                    std::int16_t bounds_min, std::int16_t bounds_max);

// ---- screen tests -----------------------------------------------------------------------------

// The projection registers of the frame (distance to the screen and the Q16 screen offset) and
// whether the whole-map far projection is on (it changes the integer screen positions).
struct ScreenProjection {
    std::int16_t h = 0;
    std::int32_t ofx = 0;
    std::int32_t ofy = 0;
    bool whole_map_far = false;
};

// The PC view the draw test widens its bounds for.
struct ScreenGeometry {
    std::int32_t screen_width = 320;   // the game's screen (4:3)
    std::int32_t screen_height = 224;
    std::int32_t render_width = 320;   // the window's render size
    std::int32_t render_height = 224;
    float pixel_aspect = 1.0f;         // horizontal pixel aspect of the PC projection
    float h_scale = 1.0f;              // horizontal field of view divisor
    float v_scale = 1.0f;              // vertical field of view widening (only > 1 counts)
    float v_shift = 0.0f;              // vertical shift of the projection centre
    std::int32_t margin = 64;          // loose margin, screen units
};

struct CullBounds {
    float hor_scale = 1.0f;     // screen x is scaled by this before the test
    std::int32_t half_width = 0;
    std::int32_t half_height = 0;
};
CullBounds cull_bounds(const ScreenGeometry& g);

// Logic visibility: is any part of the box (Q12 world bounds) on the game's own screen
// (screen_width x screen_height + 2), seen through `world_screen`.
bool box_on_screen(const Matrix& world_screen, const ScreenProjection& proj, std::int32_t screen_width,
                   std::int32_t screen_height, std::int32_t min_x, std::int32_t max_x, std::int32_t min_y,
                   std::int32_t max_y, std::int32_t min_z, std::int32_t max_z);

// Draw visibility of a box (geometry units, model space; the max corner is cut to 16 bits) seen
// through `model_view`. `far_depth` and `near_depth` are the depth limits: a box wholly beyond
// far_depth is hidden, any corner nearer than near_depth makes it visible, otherwise the corners
// (and the points where box edges cross near_depth) are tested against `bounds`.
bool box_in_frustum(const Matrix& model_view, const ScreenProjection& proj, const CullBounds& bounds,
                    std::int16_t min_x, std::int16_t min_y, std::int16_t min_z, std::int32_t max_x,
                    std::int32_t max_y, std::int32_t max_z, std::uint16_t far_depth, std::uint16_t near_depth);

// Screen regions of a projected shape: regions[row][column], row 0 above the screen, 1 on it,
// 2 below; column 0 left, 1 on, 2 right. True when the shape covers the screen centre or spans it
// from one side to the other.
bool regions_span_screen(const std::uint8_t regions[3][3]);

// One point through `model_view` and the projection; the screen position goes to sx/sy and the
// return value is the depth / 4 (0..0x3fff).
std::int32_t project_depth(const Matrix& model_view, const ScreenProjection& proj, const Vec3& point,
                           std::int16_t& sx, std::int16_t& sy);

}  // namespace sh::camera
