// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/app/loop/scene_aim.h"

#include <algorithm>
#include <cmath>

namespace sh::loop {

namespace {

constexpr double kSceneUnitsPerQ12 = 1.0 / 16.0;

using Vec3 = std::array<double, 3>;

Vec3 rotate(const std::array<double, 9>& m, const Vec3& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

// The eye of a camera whose rotation is one (camera_looking's).
Vec3 eye_of(const sh::scene::Camera& c) {
    return {-(c.rot[0] * c.trans[0] + c.rot[3] * c.trans[1] + c.rot[6] * c.trans[2]),
            -(c.rot[1] * c.trans[0] + c.rot[4] * c.trans[1] + c.rot[7] * c.trans[2]),
            -(c.rot[2] * c.trans[0] + c.rot[5] * c.trans[1] + c.rot[8] * c.trans[2])};
}

// The inverse of a camera's rotation part: the transpose but for the game's rounding of it.
std::array<double, 9> inverse(const std::array<double, 9>& m) {
    const double c00 = m[4] * m[8] - m[5] * m[7];
    const double c01 = m[5] * m[6] - m[3] * m[8];
    const double c02 = m[3] * m[7] - m[4] * m[6];
    const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (!(std::fabs(det) > 1e-9)) return {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
    return {c00 / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
            c01 / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
            c02 / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
}

}  // namespace

bool camera_looking(const double eye[3], const double at[3], sh::scene::Camera* camera) {
    double f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const double length = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    const double level = std::sqrt(f[0] * f[0] + f[2] * f[2]);
    if (!(length > 1e-6) || !(level > length * 1e-6)) return false;
    for (double& c : f) c /= length;
    // +Y is down: the right axis is level, the down axis completes the frame.
    const double r[3] = {f[2] * length / level, 0.0, -f[0] * length / level};
    const double d[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    camera->rot = {r[0], r[1], r[2], d[0], d[1], d[2], f[0], f[1], f[2]};
    const Vec3 back =
        rotate(camera->rot, {eye[0] * kSceneUnitsPerQ12, eye[1] * kSceneUnitsPerQ12, eye[2] * kSceneUnitsPerQ12});
    camera->trans = {-back[0], -back[1], -back[2]};
    return true;
}

std::array<double, 3> camera_eye(const sh::scene::Camera& camera) {
    return rotate(inverse(camera.rot), {-camera.trans[0], -camera.trans[1], -camera.trans[2]});
}

CameraGap camera_gap(const sh::scene::Camera& drawn, const sh::scene::Camera& exact) {
    CameraGap gap;
    for (std::size_t i = 0; i < 9; ++i) gap.turn = std::max(gap.turn, std::fabs(drawn.rot[i] - exact.rot[i]));
    const Vec3 seen = rotate(drawn.rot, eye_of(exact));
    const Vec3 off = {seen[0] + drawn.trans[0], seen[1] + drawn.trans[1], seen[2] + drawn.trans[2]};
    gap.move = std::sqrt(off[0] * off[0] + off[1] * off[1] + off[2] * off[2]);
    return gap;
}

void aim_scene(sh::scene::Frame& frame, sh::scene::Beam& beam, const sh::scene::Camera& to) {
    // A view-space point p of the old camera (R, t) is M p + m in the new one (R', t'): M = R' R^-1, m = t' - M t.
    const sh::scene::Camera& from = frame.camera;
    const std::array<double, 9> back = inverse(from.rot);
    std::array<double, 9> m{};
    for (std::size_t r = 0; r < 3; ++r)
        for (std::size_t c = 0; c < 3; ++c)
            for (std::size_t k = 0; k < 3; ++k) m[r * 3 + c] += to.rot[r * 3 + k] * back[k * 3 + c];
    const Vec3 mt = rotate(m, from.trans);
    const Vec3 shift = {to.trans[0] - mt[0], to.trans[1] - mt[1], to.trans[2] - mt[2]};
    const auto point = [&](float& x, float& y, float& z) {
        const Vec3 v = rotate(m, {x, y, z});
        x = static_cast<float>(v[0] + shift[0]);
        y = static_cast<float>(v[1] + shift[1]);
        z = static_cast<float>(v[2] + shift[2]);
    };
    const auto direction = [&](float& x, float& y, float& z) {
        const Vec3 v = rotate(m, {x, y, z});
        x = static_cast<float>(v[0]);
        y = static_cast<float>(v[1]);
        z = static_cast<float>(v[2]);
    };

    for (sh::scene::PrelitMesh& mesh : frame.prelit) {
        // An isolated model has its own projection: it is not in the world's view.
        if (mesh.isolated) continue;
        for (sh::scene::PrelitVertex& v : mesh.vertices) point(v.x, v.y, v.z);
        for (auto& n : mesh.patch_normals) direction(n[0], n[1], n[2]);
    }
    for (auto& flake : frame.flakes) point(flake[0], flake[1], flake[2]);
    point(beam.position[0], beam.position[1], beam.position[2]);
    point(beam.shadow_position[0], beam.shadow_position[1], beam.shadow_position[2]);
    direction(beam.direction[0], beam.direction[1], beam.direction[2]);
    frame.camera = to;
}

}  // namespace sh::loop
