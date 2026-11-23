// The view system's C entry points: the names the
// game code calls, over view_system.h and view_space.h. Arguments use the engine's layout-identical
// types; flags passed as C booleans are 4-byte words.

#include <array>
#include <cstdint>

#include "engine/camera/camera_config.h"
#include "engine/camera/camera_host.h"
#include "engine/camera/camera_matrix.h"
#include "engine/camera/camera_motion.h"
#include "engine/camera/view_system.h"
#include "engine/game_state/session.h"
#include "engine/math/fixed_geometry.h"
#include "engine/math/geometry_state.h"
#include "engine/world/chara_collision.h"

namespace cam = ::sh::camera;

namespace {

struct LongVector {
    std::int32_t x, y, z, spare;
};

struct ScreenPoint {
    std::int16_t x, y;
};

std::int32_t screen_width() { return sh::game::session().game.screen_width; }
std::int32_t screen_height() { return sh::game::session().game.screen_height; }

// The PC view the draw-side box test widens its bounds for.
cam::CullBounds draw_bounds_now() {
    cam::ScreenGeometry g = cam::camera_host().screen_geometry();
    g.screen_width = screen_width();
    g.screen_height = screen_height();
    g.margin = cam::camera_config().draw_cull_margin;
    return cam::cull_bounds(g);
}

}  // namespace

extern "C" {

// ---- the eye's matrices ----

const cam::Matrix* Vw_EyeMatrixGet(void) {
    return &cam::view_system().eye();
}

const cam::Matrix* Vw_WorldToViewMatrixGet(void) {
    return &cam::view_system().world_to_view();
}

// ---- frames and the eye ----

void Vw_CoordHierarchyMatrixCompute(cam::FrameRecord* rootCoord, cam::Matrix* transformMat) {
    *transformMat = cam::frame_world(rootCoord);
}

void Vw_MultiplyAndTransformMatrix(cam::Matrix* transformMat, cam::Matrix* inMat, cam::Matrix* outMat) {
    *outMat = cam::compose(*transformMat, *inMat);
}

void vbSetWorldScreenMatrix(cam::FrameRecord* coord) {
    cam::view_system().set_world_screen(coord);
}

void vbSetRefView(cam::ReferenceView* rview) {
    cam::view_system().set_reference_view(*rview);
}

void Vw_CoordToViewSpaceMatrix(cam::FrameRecord* rootCoord, cam::Matrix* viewMat) {
    *viewMat = cam::view_matrix(cam::view_system().world_screen(), cam::frame_world(rootCoord));
}

void Vw_CoordToWorldAndViewMatrices(cam::FrameRecord* rootCoord, cam::Matrix* worldMat, cam::Matrix* viewMat) {
    const cam::Matrix world = cam::frame_world(rootCoord);
    *worldMat = world;
    *viewMat = cam::view_matrix(cam::view_system().world_screen(), world);
}

void Vw_WorldScreenMatrixAtPositionGet(cam::Matrix* worldToScreenMat, std::int32_t posX, std::int32_t posY,
                                       std::int32_t posZ) {
    *worldToScreenMat = cam::world_screen_at(cam::camera_host().world_screen(), cam::Vec3{posX, posY, posZ});
}

// Scales the rotation columns of bone `boneIdx` of a skeleton's frames; the stale mark goes to the
// first frame, not to the scaled one.
void Chara_ModelBoneScaleSet(cam::FrameRecord* boneCoords, std::int32_t boneIdx, std::int32_t scaleX,
                             std::int32_t scaleY, std::int32_t scaleZ) {
    std::array<std::array<std::int16_t, 3>, 3> m;
    cam::Matrix& local = boneCoords[boneIdx].local;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m[r][c] = local.m[r][c];
        }
    }
    sh::world::scale_columns(m, static_cast<std::int16_t>(scaleX), static_cast<std::int16_t>(scaleY),
                             static_cast<std::int16_t>(scaleZ));
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            local.m[r][c] = m[r][c];
        }
    }
    boneCoords->world_current = 0;
}

void vwMatrixToAngleYXZ(cam::PackedAngles* ang, const cam::Matrix* mat) {
    const cam::Angles a = cam::matrix_angles(*mat);
    ang->x = a.x;
    ang->y = a.y;
    ang->z = a.z;
}

// ---- screen tests ----

// Logic visibility ("is this box on the screen?"): enemy AI, events, sound. Exactly the console's
// test -- the 4:3 screen plus 2 units -- whatever the window size, aspect or graphics preset, so
// behaviour does not depend on how much of the world the PC view shows.
std::uint32_t Vw_AabbVisibleInScreenCheck(std::int32_t minX, std::int32_t maxX, std::int32_t minY, std::int32_t maxY,
                                          std::int32_t minZ, std::int32_t maxZ) {
    return cam::box_on_screen(cam::camera_host().world_screen(), cam::projection_now(), screen_width(),
                              screen_height(), minX, maxX, minY, maxY, minZ, maxZ);
}

// Draw visibility of the original chunk path, with bounds widened for the PC view: Hor+ width,
// pixel aspect, hfov/vfov and vshift, plus the loose margin of camera.json.
std::uint32_t Vw_AabbVisibleInFrustumCheck(cam::Matrix* modelMat, std::int16_t minX, std::int16_t minY,
                                           std::int16_t minZ, std::int32_t maxX, std::int32_t maxY, std::int32_t maxZ,
                                           std::uint16_t nearPlane, std::uint16_t farPlane) {
    return cam::box_in_frustum(*modelMat, cam::projection_now(), draw_bounds_now(), minX, minY, minZ, maxX, maxY,
                               maxZ, nearPlane, farPlane);
}

std::uint32_t Vw_ScreenRegionSpanCheck(std::uint8_t (*regionFlags)[3]) {
    return cam::regions_span_screen(regionFlags);
}

// The point `worldPos` (geometry units) moved by the rotation in force and projected with the
// matrix in force; returns depth / 4.
std::int32_t Vw_TransformAndProjectPoint(LongVector* worldPos, ScreenPoint* screenPos) {
    const sh::math::GeometryState& geometry = sh::math::geometry();
    const sh::math::Matrix3& rot = geometry.rotation();
    const sh::math::Vec3i moved = sh::math::apply_matrix_lv(rot, {worldPos->x, worldPos->y, worldPos->z});

    cam::Matrix m;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            m.m[r][c] = rot.m[r][c];
        }
    }
    const std::int32_t moved_xyz[3] = {moved.x, moved.y, moved.z};
    const std::int32_t trans[3] = {geometry.translation().x, geometry.translation().y, geometry.translation().z};
    for (int i = 0; i < 3; ++i) {
        m.t[i] = static_cast<std::int32_t>(static_cast<std::uint32_t>(trans[i]) + static_cast<std::uint32_t>(moved_xyz[i]));
    }
    std::int16_t sx = 0;
    std::int16_t sy = 0;
    const std::int32_t depth = cam::project_depth(m, cam::projection_now(), cam::Vec3{}, sx, sy);
    screenPos->x = sx;
    screenPos->y = sy;
    return depth;
}

// ---- the view frame ----

void vwInitViewInfo(void) {
    cam::view_system().init();
}

cam::FrameRecord* vwGetViewCoord(void) {
    return cam::view_system().frame();
}

void vwGetViewPosition(cam::Vec3* pos) {
    *pos = cam::view_system().position();
}

void vwGetViewAngle(cam::PackedAngles* ang) {
    *ang = cam::view_system().angles();
}

void Vw_SetLookAtMatrix(const cam::Vec3* pos, const cam::Vec3* lookAt) {
    cam::view_system().set_frame(nullptr, cam::look_at_matrix(*pos, *lookAt));
}

void vwSetCoordRefAndEntou(cam::FrameRecord* parent_p, std::int32_t ref_x, std::int32_t ref_y, std::int32_t ref_z,
                           std::int16_t cam_ang_y, std::int16_t cam_ang_z, std::int32_t cam_y, std::int32_t cam_xz_r) {
    cam::view_system().set_frame(parent_p,
                                 cam::orbit_matrix(cam::Vec3{ref_x, ref_y, ref_z}, cam_ang_y, cam_ang_z, cam_y, cam_xz_r));
}

void vwSetViewInfoDirectMatrix(cam::FrameRecord* pcoord, const cam::Matrix* cammat) {
    cam::view_system().set_frame(pcoord, *cammat);
}

void vwSetViewInfo(void) {
    cam::view_system().update();
}

void Vw_ClampAngleRange(std::int16_t* angleMin, std::int16_t* angleMax, std::int16_t angleConstraintMin,
                        std::int16_t angleConstraintMax) {
    cam::clamp_angle_range(*angleMin, *angleMax, angleConstraintMin, angleConstraintMax);
}

std::int16_t Vw_LineSegmentIntersectionCheck(std::int16_t segmentLength, std::int16_t segmentDir,
                                             std::int16_t distToBound, std::int16_t boundsMin, std::int16_t boundsMax) {
    return cam::segment_cross_fraction(segmentLength, segmentDir, distToBound, boundsMin, boundsMax);
}

// ---- motion helpers ----

void vwRenewalXZVelocityToTargetPos(std::int32_t* velo_x, std::int32_t* velo_z, const cam::Vec3* now_pos,
                                    const cam::Vec3* tgt_pos, std::int32_t tgt_r, std::int32_t accel,
                                    std::int32_t total_max_spd, std::int32_t dec_forwd_lim_spd,
                                    std::int32_t dec_accel_side) {
    cam::steer_planar_velocity(*velo_x, *velo_z, *now_pos, *tgt_pos, tgt_r, accel, total_max_spd, dec_forwd_lim_spd,
                               dec_accel_side, cam::camera_host().delta_time());
}

std::int32_t vwRetNewVelocityToTargetVal(std::int32_t now_spd, std::int32_t mv_pos, std::int32_t tgt_pos,
                                         std::int32_t accel, std::int32_t total_max_spd,
                                         std::int32_t dec_val_lim_spd) {
    return cam::speed_toward(now_spd, mv_pos, tgt_pos, accel, total_max_spd, dec_val_lim_spd,
                             cam::camera_host().delta_time());
}

std::int32_t vwRetNewAngSpdToTargetAng(std::int32_t now_ang_spd, std::int16_t now_ang, std::int16_t tgt_ang,
                                       std::int32_t accel_spd, std::int32_t total_max_ang_spd,
                                       std::int32_t dec_val_lim_spd) {
    return cam::angular_speed_toward(now_ang_spd, now_ang, tgt_ang, accel_spd, total_max_ang_spd, dec_val_lim_spd,
                                     cam::camera_host().delta_time());
}

std::int32_t Vw_ClampedSpeedToTargetGet(std::int32_t speedFrom, std::int32_t speedTo, std::int32_t rateMax) {
    return cam::clamped_rate_toward(speedFrom, speedTo, rateMax, cam::camera_host().delta_time());
}

void vwAngleToVector(cam::PackedAngles* vec, const cam::PackedAngles* ang, std::int32_t r) {
    std::int16_t out[3];
    cam::direction_from_angles(ang->x, ang->y, r, out);
    vec->x = out[0];
    vec->y = out[1];
    vec->z = out[2];
}

std::int32_t vwVectorToAngle(cam::PackedAngles* ang, const cam::PackedAngles* vec) {
    const std::int16_t in[3] = {vec->x, vec->y, vec->z};
    std::int16_t angle_x = 0;
    std::int16_t angle_y = 0;
    const std::int32_t length = cam::angles_from_direction(in, angle_x, angle_y);
    ang->x = angle_x;
    ang->y = angle_y;
    ang->z = 0;
    return length;
}

std::int32_t vwOresenHokan(const std::int32_t* y_ary, std::int32_t y_suu, std::int32_t input_x, std::int32_t min_x,
                           std::int32_t max_x) {
    return cam::piecewise_linear(y_ary, y_suu, input_x, min_x, max_x);
}

}  // extern "C"
