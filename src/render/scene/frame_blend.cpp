// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/scene/frame_blend.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sh::scene {

namespace {

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<double, 9>;
using Quat = std::array<double, 4>;  // x y z w

Vec3 lerp(const Vec3& a, const Vec3& b, double t) {
    return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t};
}

double distance(const Vec3& a, const Vec3& b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

Vec3 rotate(const Mat3& m, const Vec3& v) {
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
            m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

Vec3 rotate_back(const Mat3& m, const Vec3& v) {
    return {m[0] * v[0] + m[3] * v[1] + m[6] * v[2], m[1] * v[0] + m[4] * v[1] + m[7] * v[2],
            m[2] * v[0] + m[5] * v[1] + m[8] * v[2]};
}

// Cosine of the angle of the rotation that takes `a` to `b`.
double turn_cos(const Mat3& a, const Mat3& b) {
    double trace = 0;
    for (int i = 0; i < 9; ++i) trace += a[static_cast<std::size_t>(i)] * b[static_cast<std::size_t>(i)];
    return (trace - 1.0) * 0.5;
}

Quat quat_of(const Mat3& m) {
    const double trace = m[0] + m[4] + m[8];
    Quat q;
    if (trace > 0) {
        const double s = std::sqrt(trace + 1.0) * 2;
        q = {(m[7] - m[5]) / s, (m[2] - m[6]) / s, (m[3] - m[1]) / s, 0.25 * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        const double s = std::sqrt(1.0 + m[0] - m[4] - m[8]) * 2;
        q = {0.25 * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[7] - m[5]) / s};
    } else if (m[4] > m[8]) {
        const double s = std::sqrt(1.0 + m[4] - m[0] - m[8]) * 2;
        q = {(m[1] + m[3]) / s, 0.25 * s, (m[5] + m[7]) / s, (m[2] - m[6]) / s};
    } else {
        const double s = std::sqrt(1.0 + m[8] - m[0] - m[4]) * 2;
        q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25 * s, (m[3] - m[1]) / s};
    }
    return q;
}

Mat3 matrix_of(const Quat& q) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    return {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
            2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
            2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
}

// The rotation `t` of the way from `a` to `b`. The two are a small turn apart (the caller checked), so the
// normalised straight line between the quaternions is the arc.
Mat3 blend_rotation(const Mat3& a, const Mat3& b, double t) {
    const Quat qa = quat_of(a);
    Quat qb = quat_of(b);
    const double dot = qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3];
    if (dot < 0)
        for (double& c : qb) c = -c;
    Quat q;
    double norm = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        q[i] = qa[i] + (qb[i] - qa[i]) * t;
        norm += q[i] * q[i];
    }
    norm = std::sqrt(norm);
    if (norm < 1e-12) return b;
    for (double& c : q) c /= norm;
    return matrix_of(q);
}

// View space back to the world. A camera the game rounded (each element of its rotation to 1/4096) is not a pure
// rotation, and its transpose puts a point as far off as the rounding times the point's distance from the world's
// origin: centimetres on most maps, by another amount whenever the camera turns. So the rotation is inverted.
struct Back {
    Mat3 inverse;
    Vec3 trans;
    explicit Back(const Camera& c) : trans(c.trans) {
        const Mat3& m = c.rot;
        const double c00 = m[4] * m[8] - m[5] * m[7];
        const double c01 = m[5] * m[6] - m[3] * m[8];
        const double c02 = m[3] * m[7] - m[4] * m[6];
        const double det = m[0] * c00 + m[1] * c01 + m[2] * c02;
        if (std::fabs(det) > 1e-9) {
            inverse = {c00 / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
                       c01 / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
                       c02 / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
        } else {
            inverse = {m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
        }
    }
    Vec3 point(const Vec3& v) const { return rotate(inverse, {v[0] - trans[0], v[1] - trans[1], v[2] - trans[2]}); }
    // Where the camera stands in the world.
    Vec3 eye() const { return point({0, 0, 0}); }
};

Vec3 eye(const Camera& c) { return Back(c).eye(); }

Vec3 world_of(const Back& c, const PrelitVertex& v) { return c.point({v.x, v.y, v.z}); }

Vec3 centre(const Back& c, const PrelitMesh& m) {
    Vec3 sum{0, 0, 0};
    for (const PrelitVertex& v : m.vertices) {
        sum[0] += v.x;
        sum[1] += v.y;
        sum[2] += v.z;
    }
    const double n = static_cast<double>(m.vertices.size());
    const PrelitVertex mean{static_cast<float>(sum[0] / n), static_cast<float>(sum[1] / n),
                            static_cast<float>(sum[2] / n)};
    return world_of(c, mean);
}

bool same_shape(const PrelitMesh& a, const PrelitMesh& b) {
    return a.vertices.size() == b.vertices.size() && a.indices.size() == b.indices.size() &&
           a.texture_key == b.texture_key && a.isolated == b.isolated;
}

bool is_cut(const Frame& from, const Frame& to, const BlendLimits& limits) {
    return !from.world || !to.world || distance(eye(from.camera), eye(to.camera)) > limits.camera_move ||
           turn_cos(from.camera.rot, to.camera.rot) < limits.camera_turn_cos || from.h != to.h;
}

Vec3 doubles(const std::array<float, 3>& v) { return {v[0], v[1], v[2]}; }
std::array<float, 3> floats(const Vec3& v) {
    return {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
}

Vec3 point_to_world(const Camera& c, const Vec3& v) { return Back(c).point(v); }

Vec3 point_to_view(const Camera& c, const Vec3& w) {
    const Vec3 v = rotate(c.rot, w);
    return {v[0] + c.trans[0], v[1] + c.trans[1], v[2] + c.trans[2]};
}

}  // namespace

Beam blend_beam(const Frame& from_frame, const Beam& from, const Frame& to_frame, const Beam& to,
                const Frame& out_frame, double t, const BlendLimits& limits) {
    if (!to.on || !from.on || is_cut(from_frame, to_frame, limits)) return to;
    t = std::clamp(t, 0.0, 1.0);
    const Camera& a = from_frame.camera;
    const Camera& b = to_frame.camera;
    const Camera& c = out_frame.camera;
    Beam out = to;
    out.position = floats(point_to_view(
        c, lerp(point_to_world(a, doubles(from.position)), point_to_world(b, doubles(to.position)), t)));
    out.shadow_position = floats(point_to_view(c, lerp(point_to_world(a, doubles(from.shadow_position)),
                                                        point_to_world(b, doubles(to.shadow_position)), t)));
    const Vec3 to_dir = doubles(to.direction);
    Vec3 dir = lerp(rotate_back(a.rot, doubles(from.direction)), rotate_back(b.rot, to_dir), t);
    const double length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    const double wanted = std::sqrt(to_dir[0] * to_dir[0] + to_dir[1] * to_dir[1] + to_dir[2] * to_dir[2]);
    if (length > 1e-9) {
        for (double& d : dir) d *= wanted / length;
        out.direction = floats(rotate(c.rot, dir));
    }
    return out;
}

void blend_frames(const Frame& from, const Frame& to, double t, Frame& out, const BlendLimits& limits) {
    const std::uint64_t serial = out.serial;
    out = to;
    out.serial = serial;
    t = std::clamp(t, 0.0, 1.0);

    if (is_cut(from, to, limits)) return;
    const Back back_from(from.camera);
    const Back back_to(to.camera);
    const Vec3 eye_from = back_from.eye();
    const Vec3 eye_to = back_to.eye();

    Camera& cam = out.camera;
    cam.rot = blend_rotation(from.camera.rot, to.camera.rot, t);
    const Vec3 eye_now = lerp(eye_from, eye_to, t);
    const Vec3 back = rotate(cam.rot, eye_now);
    cam.trans = {-back[0], -back[1], -back[2]};

    if (from.light.mode == to.light.mode) {
        out.light.light_pos = lerp(from.light.light_pos, to.light.light_pos, t);
        out.light.light_vec = lerp(from.light.light_vec, to.light.light_vec, t);
    }

    for (std::size_t i = 0; i < out.prelit.size(); ++i) {
        PrelitMesh& mesh = out.prelit[i];
        if (mesh.isolated || mesh.vertices.empty()) continue;
        const PrelitMesh& newer = to.prelit[i];
        const PrelitMesh* older = i < from.prelit.size() ? &from.prelit[i] : nullptr;
        if (older != nullptr && (!same_shape(*older, newer) ||
                                 distance(centre(back_from, *older), centre(back_to, newer)) > limits.mesh_move))
            older = nullptr;
        for (std::size_t v = 0; v < mesh.vertices.size(); ++v) {
            Vec3 world = world_of(back_to, newer.vertices[v]);
            if (older != nullptr) world = lerp(world_of(back_from, older->vertices[v]), world, t);
            const Vec3 view = rotate(cam.rot, world);
            mesh.vertices[v].x = static_cast<float>(view[0] + cam.trans[0]);
            mesh.vertices[v].y = static_cast<float>(view[1] + cam.trans[1]);
            mesh.vertices[v].z = static_cast<float>(view[2] + cam.trans[2]);
        }
        // Model upscaling's patch normals turn with the camera like the positions (the pose's own turn is small).
        for (std::size_t v = 0; v < mesh.patch_normals.size() && v < newer.patch_normals.size(); ++v) {
            const auto& n = newer.patch_normals[v];
            const Vec3 turned = rotate(cam.rot, rotate_back(to.camera.rot, {n[0], n[1], n[2]}));
            mesh.patch_normals[v] = {static_cast<float>(turned[0]), static_cast<float>(turned[1]),
                                     static_cast<float>(turned[2]), n[3]};
        }
    }

    for (std::size_t i = 0; i < out.objects.size() && i < from.objects.size(); ++i) {
        const ObjectRef& older = from.objects[i];
        const ObjectRef& newer = to.objects[i];
        if (older.mesh != newer.mesh || distance(older.trans, newer.trans) > limits.mesh_move ||
            turn_cos(older.rot, newer.rot) < limits.object_turn_cos)
            continue;
        out.objects[i].rot = blend_rotation(older.rot, newer.rot, t);
        out.objects[i].trans = lerp(older.trans, newer.trans, t);
    }
}

void TickFrames::record(const Frame& frame, const Beam& beam) {
    std::swap(older_, previous_);
    std::swap(previous_, last_);
    last_ = frame;
    older_beam_ = previous_beam_;
    previous_beam_ = last_beam_;
    last_beam_ = beam;
    count_ = std::min(count_ + 1, 3);
}

void TickFrames::reset() { count_ = 0; }

void TickFrames::show(double t, Frame& out, Beam& beam) const {
    if (count_ >= 3 && t < 0.0) {
        blend_frames(older_, previous_, 1.0 + t, out);
        beam = blend_beam(older_, older_beam_, previous_, previous_beam_, out, 1.0 + t);
    } else if (count_ >= 2) {
        blend_frames(previous_, last_, t, out);
        beam = blend_beam(previous_, previous_beam_, last_, last_beam_, out, t);
    } else if (count_ == 1) {
        const std::uint64_t serial = out.serial;
        out = last_;
        out.serial = serial;
        beam = last_beam_;
    }
}

TickFrames& tick_frames() {
    static TickFrames frames;
    return frames;
}

}  // namespace sh::scene
