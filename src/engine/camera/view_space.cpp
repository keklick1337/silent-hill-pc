#include "engine/camera/view_space.h"

#include <climits>

#include "engine/camera/camera_math.h"
#include "engine/camera/camera_matrix.h"
#include "engine/camera/camera_motion.h"
#include "engine/entities/fixed_transform.h"
#include "engine/world/fixed_math.h"
#include "engine/math/fixed_geometry.h"
#include "engine/math/precise_capture.h"

namespace sh::camera {

namespace {

math::Matrix3 rot3(const Matrix& m) {
    math::Matrix3 out;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.m[r][c] = m.m[r][c];
        }
    }
    return out;
}

void store_rot(Matrix& out, const math::Matrix3& m) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out.m[r][c] = m.m[r][c];
        }
    }
}

math::Transform as_transform(const Matrix& m) {
    math::Transform out;
    out.rot = rot3(m);
    out.trans = {m.t[0], m.t[1], m.t[2]};
    return out;
}

// The precise-capture camera twin when `world_screen` is the camera's integer matrix.
const math::capture::CameraTwin* twin_of(const Matrix& world_screen) {
    const math::Transform t = as_transform(world_screen);
    return math::capture::camera_of(t.rot, t.trans);
}

std::int32_t wrap_add(std::int32_t a, std::int32_t b) { return world::wrap_add(a, b); }
std::int32_t wrap_sub(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

Matrix rotation_of(const Angles& a) {
    Matrix m;
    store_rot(m, math::rot_matrix_zxy_neg({a.x, a.y, a.z}));
    return m;
}

math::Projection projection_of(const Matrix& model_view, const ScreenProjection& proj) {
    math::Projection p;
    p.rot = rot3(model_view);
    p.trans = {model_view.t[0], model_view.t[1], model_view.t[2]};
    p.h = proj.h;
    p.ofx = proj.ofx;
    p.ofy = proj.ofy;
    return p;
}

math::Projected project(const math::Projection& p, bool whole_map_far, const math::Vec3s& v) {
    if (whole_map_far) {
        return math::project_vertex_far(p, v).v;
    }
    return math::project_vertex(p, v);
}

// Screen x and y bands of a projected point: 0 before the bound, 1 inside, 2 past it.
int band(std::int32_t v, std::int32_t half) {
    if (v < -half) {
        return 0;
    }
    return half < v ? 2 : 1;
}

}  // namespace

// ---- frame hierarchy --------------------------------------------------------------------------

Matrix compose(const Matrix& parent, const Matrix& local) {
    entities::BoneTransform p;
    entities::BoneTransform l;
    for (int i = 0; i < 9; ++i) {
        p.rotation[static_cast<std::size_t>(i)] = parent.m[i / 3][i % 3];
        l.rotation[static_cast<std::size_t>(i)] = local.m[i / 3][i % 3];
    }
    for (int i = 0; i < 3; ++i) {
        p.translation[static_cast<std::size_t>(i)] = parent.t[i];
        l.translation[static_cast<std::size_t>(i)] = local.t[i];
    }
    const entities::BoneTransform r = entities::compose(p, l);
    Matrix out;
    for (int i = 0; i < 9; ++i) {
        out.m[i / 3][i % 3] = r.rotation[static_cast<std::size_t>(i)];
    }
    for (int i = 0; i < 3; ++i) {
        out.t[i] = r.translation[static_cast<std::size_t>(i)];
    }
    return out;
}

Matrix frame_world(FrameHost& host, FrameHost::Frame frame) {
    if (!host.usable(frame)) {
        if (frame != nullptr) {
            host.unusable_root(frame);
        }
        return rotation_of(Angles{});
    }

    // Climb to the topmost frame whose world is stale, threading child links on the way so the
    // descent can come back down the same chain.
    FrameHost::Frame top = nullptr;
    for (FrameHost::Frame f = frame;;) {
        FrameHost::Frame up = host.parent(f);
        if (host.world_current(f)) {
            break;
        }
        top = f;
        if (up == nullptr) {
            break;
        }
        host.set_child(up, f);
        f = up;
    }

    if (top != nullptr) {
        host.set_child(frame, nullptr);
        for (FrameHost::Frame f = top; f != nullptr; f = host.child(f)) {
            FrameHost::Frame up = host.parent(f);
            host.mark_world_current(f);
            host.set_world(f, up == nullptr ? host.local(f) : compose(host.world(up), host.local(f)));
        }
    }
    return host.world(frame);
}

// ---- eye and world-screen matrix --------------------------------------------------------------

WorldScreen world_screen_from_eye(const Matrix& eye_world) {
    WorldScreen ws;
    ws.eye = eye_world;
    ws.world_to_view = transpose(eye_world);
    ws.world_to_view.t[0] = ws.world_to_view.t[1] = ws.world_to_view.t[2] = 0;

    ws.world_screen = ws.world_to_view;
    const math::Vec3i moved = math::apply_matrix_lv(
        rot3(ws.world_to_view),
        {wrap_sub(0, eye_world.t[0]), wrap_sub(0, eye_world.t[1]), wrap_sub(0, eye_world.t[2])});
    ws.world_screen.t[0] = moved.x;
    ws.world_screen.t[1] = moved.y;
    ws.world_screen.t[2] = moved.z;
    return ws;
}

Matrix reference_view_local(const Vec3& eye, const Vec3& target) {
    // The direction is kept in 16 bits, like the short vector the angles are taken from.
    const std::int16_t dir[3] = {static_cast<std::int16_t>(target.x - eye.x),
                                 static_cast<std::int16_t>(target.y - eye.y),
                                 static_cast<std::int16_t>(target.z - eye.z)};
    Angles a;
    (void)angles_from_direction(dir, a.x, a.y);
    Matrix m = rotation_of(a);
    m.t[0] = eye.x;
    m.t[1] = eye.y;
    m.t[2] = eye.z;
    return m;
}

Matrix view_matrix(const WorldScreen& ws, Matrix world) {
    const Matrix placed = world;
    for (int i = 0; i < 3; ++i) {
        world.t[i] = wrap_sub(world.t[i], ws.eye.t[i]);
    }
    Matrix out = compose(ws.world_to_view, world);
    // compose turns a point through 16-bit components (a bone's offset from its parent always fits). The offset from
    // the eye does not: past 128 m it would wrap, and a camera flown far from a model would see it again at a wrapped
    // place. Turned in full width here; the same numbers while the offset fits 16 bits.
    for (int r = 0; r < 3; ++r) {
        std::int64_t sum = std::int64_t{ws.world_to_view.t[r]} * 4096;
        for (int k = 0; k < 3; ++k) {
            sum += std::int64_t{ws.world_to_view.m[r][k]} * world.t[k];
        }
        out.t[r] = static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(sum >> 12)));
    }
    // The unquantised camera's view of the same model, for the drawer that projects this matrix.
    if (const math::capture::CameraTwin* c = math::capture::camera() ? twin_of(ws.world_screen) : nullptr) {
        double rot[9], trans[3];
        for (int r = 0; r < 3; ++r) {
            for (int k = 0; k < 3; ++k) {
                rot[r * 3 + k] = (c->rot[r * 3] * placed.m[0][k] + c->rot[r * 3 + 1] * placed.m[1][k] +
                                  c->rot[r * 3 + 2] * placed.m[2][k]) /
                                 4096.0;
            }
            trans[r] = c->rot[r * 3] * (placed.t[0] - c->eye[0]) + c->rot[r * 3 + 1] * (placed.t[1] - c->eye[1]) +
                       c->rot[r * 3 + 2] * (placed.t[2] - c->eye[2]);
        }
        math::capture::register_derived(as_transform(out), rot, trans);
    }
    return out;
}

Matrix world_screen_at(const Matrix& world_screen, const Vec3& pos) {
    const math::Vec3i moved = math::apply_matrix_lv(rot3(world_screen), {pos.x >> 4, pos.y >> 4, pos.z >> 4});
    Matrix out = world_screen;
    out.t[0] = wrap_add(moved.x, world_screen.t[0]);
    out.t[1] = wrap_add(moved.y, world_screen.t[1]);
    out.t[2] = wrap_add(moved.z, world_screen.t[2]);
    if (math::capture::camera() != nullptr) {
        // The point before its Q12-to-Q8 truncation, seen by the unquantised camera.
        const double point[3] = {pos.x / 16.0, pos.y / 16.0, pos.z / 16.0};
        math::capture::register_camera_point(as_transform(world_screen), point, as_transform(out));
    }
    return out;
}

Matrix look_at_matrix(const Vec3& pos, const Vec3& target) {
    const std::int32_t dx = wrap_sub(target.x, pos.x) >> 4;
    const std::int32_t dy = wrap_sub(target.y, pos.y) >> 4;
    const std::int32_t dz = wrap_sub(target.z, pos.z) >> 4;

    Angles a;
    a.y = static_cast<std::int16_t>(fx::atan2(dx, dz));
    a.x = static_cast<std::int16_t>(fx::atan2(-dy, world::planar_length(dx, dz)));
    Matrix m = rotation_of(a);
    m.t[0] = pos.x >> 4;
    m.t[1] = pos.y >> 4;
    m.t[2] = pos.z >> 4;
    return m;
}

Matrix orbit_matrix(const Vec3& ref, std::int32_t yaw, std::int32_t roll, std::int32_t height,
                    std::int32_t xz_radius) {
    Angles a;
    a.x = static_cast<std::int16_t>(-fx::atan2(-height, xz_radius));
    a.y = static_cast<std::int16_t>((yaw + 2048) & 0xFFF);
    a.z = static_cast<std::int16_t>(roll);
    Matrix m = rotation_of(a);
    const std::int32_t r = xz_radius >> 4;
    m.t[0] = wrap_add(ref.x >> 4, fx::wrap_mul(r, fx::sin(yaw)) >> 12);
    m.t[1] = wrap_add(ref.y >> 4, height >> 4);
    m.t[2] = wrap_add(ref.z >> 4, fx::wrap_mul(r, fx::cos(yaw)) >> 12);
    return m;
}

ViewPose pose_of(const Matrix& world) {
    ViewPose p;
    p.position = {world.t[0] << 4, world.t[1] << 4, world.t[2] << 4};
    p.angles = matrix_angles(world);
    return p;
}

// ---- camera workspace helpers ----------------------------------------------------------------

std::int32_t vector_length(std::int32_t x, std::int32_t y, std::int32_t z) { return fx::length(x, y, z); }

Vec3 now_watch_position(const Vec3& cam_pos, const Vec3& watch_target, const Angles& cam_rotation) {
    const std::int32_t cos_x = fx::cos(cam_rotation.x);
    const std::int32_t sin_x = fx::sin(cam_rotation.x);
    const std::int32_t cos_y = fx::cos(cam_rotation.y);
    const std::int32_t sin_y = fx::sin(cam_rotation.y);
    const std::int32_t r = vector_length(wrap_sub(cam_pos.x, watch_target.x), wrap_sub(cam_pos.y, watch_target.y),
                                         wrap_sub(cam_pos.z, watch_target.z));
    Vec3 out;
    out.x = wrap_add(fx::mul_wide(fx::mul_wide(r, sin_y), cos_x), cam_pos.x);
    out.z = wrap_add(fx::mul_wide(fx::mul_wide(r, cos_y), cos_x), cam_pos.z);
    out.y = wrap_sub(cam_pos.y, fx::mul_wide(r, sin_x));
    return out;
}

Matrix attached_look_at(const Matrix& frame_world, const Angles& rotation, const Vec3& pos) {
    Matrix out;
    store_rot(out, math::mul_matrix(rot3(frame_world), rot3(rotation_of(rotation))));
    out.t[0] = pos.x >> 4;
    out.t[1] = pos.y >> 4;
    out.t[2] = pos.z >> 4;
    return out;
}

// ---- angle and segment helpers ----------------------------------------------------------------

void clamp_angle_range(std::int16_t& min, std::int16_t& max, std::int16_t limit_min, std::int16_t limit_max) {
    const std::int32_t base = min;
    // Everything relative to `min`, folded into one turn.
    std::int32_t hi = (max - base) & 0xFFF;
    const std::int16_t lim_lo = static_cast<std::int16_t>((limit_min - base) & 0xFFF);
    const std::int16_t lim_hi = static_cast<std::int16_t>((limit_max - base) & 0xFFF);

    std::int32_t lo = 0;
    if (lim_lo <= lim_hi) {
        if (lim_lo > lo) {
            lo = lim_lo;
        }
        if (lim_hi < hi) {
            hi = lim_hi;
        }
        if (hi < lo) {
            lo = hi;
        }
    } else if (lim_hi > lo && lim_hi < hi) {
        hi = lim_hi;
    }

    min = static_cast<std::int16_t>((lo + base) & 0xFFF);
    max = static_cast<std::int16_t>((hi + base) & 0xFFF);
}

std::int16_t segment_cross_fraction(std::int16_t length, std::int16_t direction, std::int16_t distance,
                                    std::int16_t bounds_min, std::int16_t bounds_max) {
    constexpr std::int16_t kOne = 4096;
    // The segment must head toward the line (or lie on it).
    if (direction > 0) {
        if (distance < 0) {
            return kOne;
        }
    } else if (direction >= 0 || distance > 0) {
        return kOne;
    }

    if (distance == 0) {
        return (bounds_min <= 0 && bounds_max >= 0) ? 0 : kOne;
    }
    if (fx::abs32(distance) > fx::abs32(direction)) {
        return kOne;
    }

    const std::int16_t fraction = static_cast<std::int16_t>(((distance << 16) >> 4) / direction);
    const std::int32_t along = (length * fraction) >> 12;
    if (along < bounds_min || bounds_max < along) {
        return kOne;
    }
    return fraction;
}

// ---- screen tests -----------------------------------------------------------------------------

CullBounds cull_bounds(const ScreenGeometry& g) {
    CullBounds b;
    const float screen_w = static_cast<float>(g.screen_width);
    const float render_w = static_cast<float>(g.render_width);
    b.hor_scale = screen_w > 0.0f ? render_w / screen_w : 1.0f;
    const float screen_aspect =
        g.screen_height > 0 ? (screen_w / static_cast<float>(g.screen_height)) : (4.0f / 3.0f);
    const float render_aspect =
        g.render_height > 0 ? (render_w / static_cast<float>(g.render_height)) : screen_aspect;
    const float h_div = g.h_scale > 0.01f ? g.h_scale : 1.0f;
    b.half_width = static_cast<std::int32_t>((screen_w * 0.5f) * b.hor_scale * (render_aspect / screen_aspect) *
                                                 g.pixel_aspect / h_div +
                                             0.5f) +
                   g.margin;

    const float v_widen = g.v_scale > 1.0f ? g.v_scale : 1.0f;
    const float v_shift = g.v_shift < 0.0f ? -g.v_shift : g.v_shift;
    b.half_height =
        static_cast<std::int32_t>(static_cast<float>(g.screen_height >> 1) * v_widen + v_shift + 0.5f) + g.margin;
    return b;
}

bool box_on_screen(const Matrix& world_screen, const ScreenProjection& proj, std::int32_t screen_width,
                   std::int32_t screen_height, std::int32_t min_x, std::int32_t max_x, std::int32_t min_y,
                   std::int32_t max_y, std::int32_t min_z, std::int32_t max_z) {
    const Matrix at = world_screen_at(world_screen, Vec3{min_x, min_y, min_z});
    const math::Projection p = projection_of(at, proj);

    std::int32_t lo_x = INT_MAX;
    std::int32_t lo_y = INT_MAX;
    std::int32_t hi_x = INT_MIN;
    std::int32_t hi_y = INT_MIN;
    for (int corner = 0; corner < 8; ++corner) {
        const math::Vec3s v{static_cast<std::int16_t>((corner & 1) ? (max_x - min_x) >> 4 : 0),
                           static_cast<std::int16_t>((corner & 2) ? (max_y - min_y) >> 4 : 0),
                           static_cast<std::int16_t>((corner & 4) ? (max_z - min_z) >> 4 : 0)};
        const math::Projected s = project(p, proj.whole_map_far, v);
        // Only corners in front of the eye and inside the depth range count.
        const std::uint32_t depth = static_cast<std::uint32_t>(s.sz >> 2) - 1u;
        if (depth < 0x3FFE) {
            hi_x = hi_x > s.sx ? hi_x : s.sx;
            lo_x = lo_x < s.sx ? lo_x : s.sx;
            hi_y = hi_y > s.sy ? hi_y : s.sy;
            lo_y = lo_y < s.sy ? lo_y : s.sy;
        }
    }
    if (hi_x == INT_MIN) {
        return false;
    }

    const std::int32_t half_w = screen_width / 2 + 2;
    const std::int32_t half_h = screen_height / 2 + 2;
    return !(hi_x < -half_w || half_w < lo_x || hi_y < -half_h || half_h < lo_y);
}

bool regions_span_screen(const std::uint8_t regions[3][3]) {
    if (regions[1][1]) {
        return true;
    }
    const bool left = regions[1][0] || (regions[0][0] && regions[2][0]);
    const bool right = regions[1][2] || (regions[0][2] && regions[2][2]);
    if (left && right) {
        return true;
    }
    const bool top = regions[0][1] || (regions[0][0] && regions[0][2]);
    const bool bottom = regions[2][1] || (regions[2][0] && regions[2][2]);
    return top && bottom;
}

bool box_in_frustum(const Matrix& model_view, const ScreenProjection& proj, const CullBounds& bounds,
                    std::int16_t min_x, std::int16_t min_y, std::int16_t min_z, std::int32_t max_x,
                    std::int32_t max_y, std::int32_t max_z, std::uint16_t far_depth, std::uint16_t near_depth) {
    const std::int16_t hx = static_cast<std::int16_t>(max_x);
    const std::int16_t hy = static_cast<std::int16_t>(max_y);
    const std::int16_t hz = static_cast<std::int16_t>(max_z);
    // Corners: bottom face 0..3 then top face 4..7, each face going round.
    const math::Vec3s corners[8] = {{min_x, min_y, min_z}, {hx, min_y, min_z}, {hx, hy, min_z}, {min_x, hy, min_z},
                                   {min_x, min_y, hz},    {hx, min_y, hz},    {hx, hy, hz},    {min_x, hy, hz}};
    const math::Projection p = projection_of(model_view, proj);

    std::uint8_t cols[3] = {};
    std::uint8_t rows[3] = {};
    std::uint8_t regions[3][3] = {};
    auto classify = [&](std::int16_t sx, std::int16_t sy, bool region) {
        const int col = band(static_cast<std::int32_t>(sx * bounds.hor_scale), bounds.half_width);
        const int row = band(sy, bounds.half_height);
        cols[col] = 1;
        rows[row] = 1;
        if (region) {
            regions[row][col] = 1;
        }
    };

    int beyond_far = 0;
    int nearer = 0;
    for (const math::Vec3s& c : corners) {
        const math::Projected s = project(p, proj.whole_map_far, c);
        const std::int32_t depth = static_cast<std::int32_t>(s.sz >> 2) * 4;
        if (far_depth < depth) {
            ++beyond_far;
        }
        if (depth < near_depth) {
            ++nearer;
        } else {
            classify(s.sx, s.sy, true);
        }
    }

    if (beyond_far == 8) {
        return false;
    }
    // A corner nearer than the near depth: near geometry is what the bottom of the screen shows.
    if (nearer > 0) {
        return true;
    }
    if (regions_span_screen(regions)) {
        return true;
    }

    // The corners alone do not decide: add the points where box edges cross the near depth.
    std::int32_t view_z[8];
    for (int i = 0; i < 8; ++i) {
        view_z[i] = math::matrix_vector(p.rot, corners[i], math::Offset::Translation, p.trans).full.z;
    }
    static constexpr std::uint8_t kEdges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                                   {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : kEdges) {
        const std::int32_t z0 = view_z[e[0]];
        const std::int32_t z1 = view_z[e[1]];
        if (z0 == z1) {
            continue;
        }
        const bool crosses = near_depth < z0 ? near_depth >= z1 : near_depth < z1;
        if (!crosses) {
            continue;
        }
        const std::int32_t t = fx::wrap_mul(near_depth - z0, 256) / (z1 - z0);
        const math::Vec3s& a = corners[e[0]];
        const math::Vec3s& b = corners[e[1]];
        const math::Vec3s cross{static_cast<std::int16_t>(a.x + (fx::wrap_mul(t, b.x - a.x) >> 8)),
                               static_cast<std::int16_t>(a.y + (fx::wrap_mul(t, b.y - a.y) >> 8)),
                               static_cast<std::int16_t>(a.z + (fx::wrap_mul(t, b.z - a.z) >> 8))};
        const math::Projected s = project(p, proj.whole_map_far, cross);
        classify(s.sx, s.sy, false);
    }

    if (!cols[0] && !cols[1]) {
        return false;
    }
    if (!cols[2] && !cols[1]) {
        return false;
    }
    if (!rows[0] && !rows[1]) {
        return false;
    }
    if (rows[2]) {
        return true;
    }
    return rows[1] != 0;
}

std::int32_t project_depth(const Matrix& model_view, const ScreenProjection& proj, const Vec3& point,
                           std::int16_t& sx, std::int16_t& sy) {
    const math::Projected s =
        project(projection_of(model_view, proj), proj.whole_map_far,
                {static_cast<std::int16_t>(point.x), static_cast<std::int16_t>(point.y),
                 static_cast<std::int16_t>(point.z)});
    sx = s.sx;
    sy = s.sy;
    return s.sz >> 2;
}

}  // namespace sh::camera
