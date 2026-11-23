#include "engine/camera/precise_view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "engine/math/precise_capture.h"
#include "render/gpu/render_state.h"

namespace sh::camera {

namespace {

constexpr double kUnitsPerRadian = 4096.0 / (2.0 * std::numbers::pi);

// One camera angle in angle units. The rig's angles move in whole units, often 1, 2, 1, 1, 2 per frame for a steady
// turn. The drawn angle follows the integer one at its mean rate over the last few frames and is pulled back into
// three quarters of a unit of it, so a steady turn is drawn steady while the camera never leaves the game's by more
// than that (about 0.07 degrees). Only past frames are used: no latency is added.
class AngleTrack {
public:
    double step(double angle) {
        if (count_ == 0) {
            unwrapped_ = angle;
            drawn_ = angle;
        } else {
            double d = std::remainder(angle - last_, 4096.0);
            unwrapped_ += d;
            if (std::fabs(d) > kCut) {
                // A cut: no rate across it.
                count_ = 0;
                drawn_ = unwrapped_;
            }
        }
        last_ = angle;
        history_[static_cast<std::size_t>(head_)] = unwrapped_;
        head_ = (head_ + 1) % kSpan;
        count_ = std::min(count_ + 1, kSpan);
        if (count_ > 1) {
            const double oldest = history_[static_cast<std::size_t>((head_ - count_ + kSpan) % kSpan)];
            const double rate = (unwrapped_ - oldest) / (count_ - 1);
            drawn_ = std::clamp(drawn_ + rate, unwrapped_ - kWindow, unwrapped_ + kWindow);
        }
        return drawn_;
    }
    void reset() { count_ = 0; head_ = 0; }

private:
    static constexpr int kSpan = 4;
    static constexpr double kWindow = 0.75;
    static constexpr double kCut = 256.0;
    std::array<double, kSpan> history_{};
    int count_ = 0;
    int head_ = 0;
    double unwrapped_ = 0;
    double last_ = 0;
    double drawn_ = 0;
};

// The rig's rotation_yxz (camera_matrix.cpp) in double, at unit scale.
void rotation_yxz(double ax, double ay, double az, double out[9]) {
    const double sx = std::sin(ax / kUnitsPerRadian), cx = std::cos(ax / kUnitsPerRadian);
    const double sy = std::sin(ay / kUnitsPerRadian), cy = std::cos(ay / kUnitsPerRadian);
    const double sz = std::sin(az / kUnitsPerRadian), cz = std::cos(az / kUnitsPerRadian);
    out[0] = cy * cz + sy * sx * sz;
    out[1] = -cy * sz + sy * sx * cz;
    out[2] = sy * cx;
    out[3] = cx * sz;
    out[4] = cx * cz;
    out[5] = -sx;
    out[6] = -sy * cz + cy * sx * sz;
    out[7] = sy * sz + cy * sx * cz;
    out[8] = cy * cx;
}

void multiply(const double a[9], const double b[9], double out[9]) {
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
}

struct Candidate {
    bool valid = false;
    Matrix key;  // the integer eye matrix it stands for
    double rot[9] = {};
    double eye[3] = {};
};

Candidate s_candidate;
Candidate s_free;  // the free camera's: it does not go through the rig
std::array<AngleTrack, 3> s_tracks;

bool same(const Matrix& a, const Matrix& b) {
    for (int r = 0; r < 3; ++r) {
        if (a.t[r] != b.t[r]) return false;
        for (int c = 0; c < 3; ++c)
            if (a.m[r][c] != b.m[r][c]) return false;
    }
    return true;
}

}  // namespace

void update_precise_camera(const CameraState& state, const Matrix& view_out, bool direct) {
    s_candidate.valid = false;
    if (!sh::render::smooth_view() || !direct) {
        for (AngleTrack& t : s_tracks) t.reset();
        return;
    }
    // The rig's camera: base rotation times the offset rotation, each from its integer angles, without the Q12
    // rounding of the two matrices and their product.
    double base[9], offset[9], r[9];
    rotation_yxz(state.base_cam_rotation.x, state.base_cam_rotation.y, state.base_cam_rotation.z, base);
    rotation_yxz(state.cam_rotation_offset.x, state.cam_rotation_offset.y, state.cam_rotation_offset.z, offset);
    multiply(base, offset, r);
    // Its angles in the same YXZ order (pitch below 90 degrees, as the rig keeps it), smoothed.
    const double ax = std::asin(std::clamp(-r[5], -1.0, 1.0)) * kUnitsPerRadian;
    const double ay = std::atan2(r[2], r[8]) * kUnitsPerRadian;
    const double az = std::atan2(r[3], r[4]) * kUnitsPerRadian;
    rotation_yxz(s_tracks[0].step(ax), s_tracks[1].step(ay), s_tracks[2].step(az), s_candidate.rot);

    // The eye before the Q12-to-Q8 truncation of the camera matrix.
    s_candidate.eye[0] = state.cam_pos.x / 16.0;
    s_candidate.eye[1] = state.cam_pos.y / 16.0;
    s_candidate.eye[2] = state.cam_pos.z / 16.0;

    // The double camera must be the integer one up to the steps it removes; anything further off is a camera this
    // module misread, drawn from the integers.
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(s_candidate.eye[i] - view_out.t[i]) > 1.5) return;
        for (int c = 0; c < 3; ++c)
            if (std::fabs(s_candidate.rot[i * 3 + c] * 4096.0 - view_out.m[i][c]) > 24.0) return;
    }
    s_candidate.key = view_out;
    s_candidate.valid = true;
}

void set_free_camera_view(const Matrix& eye_matrix, double yaw, double pitch, const double position[3]) {
    s_free.valid = false;
    if (!sh::render::smooth_view()) return;
    // look_at_matrix's angles: the heading as is, the tilt against the pitch (+Y is down).
    rotation_yxz(-pitch, yaw, 0.0, s_free.rot);
    for (int i = 0; i < 3; ++i) s_free.eye[i] = position[i] / 16.0;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(s_free.eye[i] - eye_matrix.t[i]) > 1.5) return;
        for (int c = 0; c < 3; ++c)
            if (std::fabs(s_free.rot[i * 3 + c] * 4096.0 - eye_matrix.m[i][c]) > 24.0) return;
    }
    s_free.key = eye_matrix;
    s_free.valid = true;
}

void clear_free_camera_view() { s_free.valid = false; }

void publish_precise_view(const Matrix& eye_world, const WorldScreen& ws) {
    if (!sh::render::smooth_view()) {
        sh::math::capture::clear_camera();
        return;
    }
    double eye_rot[9], eye[3];
    if (s_free.valid && same(s_free.key, eye_world)) {
        std::copy(s_free.rot, s_free.rot + 9, eye_rot);
        std::copy(s_free.eye, s_free.eye + 3, eye);
    } else if (s_candidate.valid && same(s_candidate.key, eye_world)) {
        std::copy(s_candidate.rot, s_candidate.rot + 9, eye_rot);
        std::copy(s_candidate.eye, s_candidate.eye + 3, eye);
    } else {
        // Any other eye (a scripted view, a frame under a bone) is drawn from its integers, still as the one camera
        // every path shares.
        for (int i = 0; i < 9; ++i) eye_rot[i] = eye_world.m[i / 3][i % 3] / 4096.0;
        for (int i = 0; i < 3; ++i) eye[i] = eye_world.t[i];
    }
    sh::math::capture::CameraTwin twin;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) twin.key.rot.m[r][c] = ws.world_screen.m[r][c];
        twin.eye[r] = eye[r];
    }
    twin.key.trans = {ws.world_screen.t[0], ws.world_screen.t[1], ws.world_screen.t[2]};
    // World to view is the eye rotation transposed; the world-screen translation moves the eye to the origin.
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) twin.rot[r * 3 + c] = eye_rot[c * 3 + r];
    for (int r = 0; r < 3; ++r)
        twin.trans[r] = -(twin.rot[r * 3] * eye[0] + twin.rot[r * 3 + 1] * eye[1] + twin.rot[r * 3 + 2] * eye[2]);
    sh::math::capture::set_camera(twin);
}

}  // namespace sh::camera
