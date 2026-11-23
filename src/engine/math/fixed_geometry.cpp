// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/math/fixed_geometry.h"

#include <bit>
#include <cmath>

#include "engine/world/fixed_math.h"
#include "engine/world/fixed_trig.h"

namespace sh::math {

namespace {

using i64 = std::int64_t;

// Reciprocal seed of the divide: table[i] = max(0, ((0x40000 / (i + 0x100) + 1) / 2) - 0x101) for i in 0..256.
constexpr std::array<std::uint8_t, 257> make_reciprocal_table() {
    std::array<std::uint8_t, 257> t{};
    for (int i = 0; i < 257; ++i) {
        const int v = ((0x40000 / (i + 0x100) + 1) / 2) - 0x101;
        t[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v > 0 ? v : 0);
    }
    return t;
}
constexpr std::array<std::uint8_t, 257> kReciprocal = make_reciprocal_table();

// A Q12 product sum brought back to the vector's scale, kept as its 32-bit truncation.
std::int32_t q12(i64 sum) { return static_cast<std::int32_t>(sum >> 12); }

std::int16_t saturate16(std::int32_t v, bool clamp_to_zero) {
    const std::int32_t lo = clamp_to_zero ? 0 : -0x8000;
    return static_cast<std::int16_t>(v > 0x7fff ? 0x7fff : (v < lo ? lo : v));
}

Vec3s saturate16(const Vec3i& v, bool clamp_to_zero) {
    return {saturate16(v.x, clamp_to_zero), saturate16(v.y, clamp_to_zero), saturate16(v.z, clamp_to_zero)};
}

std::uint8_t saturate8(std::int32_t v) { return static_cast<std::uint8_t>(v > 0xff ? 0xff : (v < 0 ? 0 : v)); }

std::int16_t screen_clamp(i64 v) { return static_cast<std::int16_t>(v > 0x3ff ? 0x3ff : (v < -0x400 ? -0x400 : v)); }

// A colour from its Q4 components: the 16-bit saturation and the 8-bit colour.
ColorOut finish_color(std::int32_t r, std::int32_t g, std::int32_t b, bool clamp_to_zero) {
    ColorOut o;
    o.full = {r, g, b};
    o.saturated = saturate16(o.full, clamp_to_zero);
    o.rgb = {saturate8(r >> 4), saturate8(g >> 4), saturate8(b >> 4)};
    return o;
}

// One component blended towards the far colour: base + cue * (far - base), the difference saturated to 16 bits.
std::int32_t blend(std::int32_t base, std::int32_t far, std::int16_t cue) {
    const std::int32_t diff = saturate16(q12((static_cast<i64>(far) << 12) - base), false);
    return q12(base + static_cast<i64>(cue) * diff);
}

}  // namespace

std::uint32_t divide(std::uint16_t numerator, std::uint16_t denominator) {
    if (numerator < static_cast<std::uint32_t>(denominator) * 2) {
        const int shift = std::countl_zero(static_cast<std::uint16_t>(denominator));
        const std::uint32_t d = static_cast<std::uint32_t>(denominator) << shift;
        const std::int32_t r1 = static_cast<std::int32_t>(d) & 0x7fff;
        const std::int32_t seed = kReciprocal[static_cast<std::size_t>((r1 + 0x40) >> 7)] + 0x101;
        const std::int32_t r3 = ((0x80 - seed * (r1 + 0x8000)) >> 8) & 0x1ffff;
        const std::uint32_t reciprocal = static_cast<std::uint32_t>((seed * r3 + 0x80) >> 8);
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(reciprocal) * (static_cast<std::uint32_t>(numerator) << shift) + 0x8000) >> 16);
    }
    return 0xffffffffu;
}

Projected project_vertex(const Projection& p, const Vec3s& v) {
    const auto& r = p.rot.m;
    Projected o;
    const i64 sum1 = (static_cast<i64>(p.trans.x) << 12) + r[0][0] * v.x + r[0][1] * v.y + r[0][2] * v.z;
    const i64 sum2 = (static_cast<i64>(p.trans.y) << 12) + r[1][0] * v.x + r[1][1] * v.y + r[1][2] * v.z;
    const i64 sum3 = (static_cast<i64>(p.trans.z) << 12) + r[2][0] * v.x + r[2][1] * v.y + r[2][2] * v.z;
    o.view = {q12(sum1), q12(sum2), q12(sum3)};
    o.view_short = saturate16(o.view, false);
    const std::int32_t depth = static_cast<std::int32_t>(sum3 >> 12);
    o.depth_clamped = depth > 0xffff || depth < 0;
    o.sz = static_cast<std::uint16_t>(depth > 0xffff ? 0xffff : (depth < 0 ? 0 : depth));
    o.h_over_sz = divide(static_cast<std::uint16_t>(p.h), o.sz);
    if (o.h_over_sz == 0xffffffffu) {
        o.too_near = true;
        o.h_over_sz = 0x1ffff;
    } else if (o.h_over_sz > 0x1ffff) {
        o.h_over_sz = 0x1ffff;
    }
    const i64 fx = static_cast<i64>(p.ofx) + static_cast<i64>(o.view_short.x) * o.h_over_sz;
    const i64 fy = static_cast<i64>(p.ofy) + static_cast<i64>(o.view_short.y) * o.h_over_sz;
    o.sx = screen_clamp(fx >> 16);
    o.sy = screen_clamp(fy >> 16);
    return o;
}

std::int16_t depth_cue(const Projection& p, std::uint32_t h_over_sz) {
    const i64 value = static_cast<i64>(p.dqb) + static_cast<i64>(p.dqa) * h_over_sz;
    const std::int32_t in12 = static_cast<std::int32_t>(value >> 12);
    return static_cast<std::int16_t>(in12 > 0x1000 ? 0x1000 : (in12 < 0 ? 0 : in12));
}

ProjectedFar project_vertex_far(const Projection& p, const Vec3s& v) {
    ProjectedFar out;
    out.v = project_vertex(p, v);
    const auto& r = p.rot.m;
    const i64 sum3 = (static_cast<i64>(p.trans.z) << 12) + r[2][0] * v.x + r[2][1] * v.y + r[2][2] * v.z;
    const i64 fz = sum3 >> 12;
    out.unclamped_depth = fz > 0x7fffffffLL ? 0x7fffffff : (fz < 0 ? 0 : static_cast<std::int32_t>(fz));
    if (fz > 0 && (fz > 0xffff || out.v.view.x != out.v.view_short.x || out.v.view.y != out.v.view_short.y)) {
        const double ratio = static_cast<double>(p.h) / static_cast<double>(fz);
        const double fsx = static_cast<double>(p.ofx) / 65536.0 + static_cast<double>(out.v.view.x) * ratio;
        const double fsy = static_cast<double>(p.ofy) / 65536.0 + static_cast<double>(out.v.view.y) * ratio;
        const auto round_away = [](double d) { return static_cast<i64>(d + (d >= 0.0 ? 0.5 : -0.5)); };
        out.v.sx = screen_clamp(round_away(fsx));
        out.v.sy = screen_clamp(round_away(fsy));
    }
    return out;
}

MatVec matrix_vector(const Matrix3& m, const Vec3s& v, Offset o, const Vec3i& offset, bool clamp_to_zero) {
    const auto& a = m.m;
    const i64 add_x = o == Offset::None ? 0 : static_cast<i64>(offset.x) << 12;
    const i64 add_y = o == Offset::None ? 0 : static_cast<i64>(offset.y) << 12;
    const i64 add_z = o == Offset::None ? 0 : static_cast<i64>(offset.z) << 12;
    MatVec out;
    out.full = {q12(add_x + a[0][0] * v.x + a[0][1] * v.y + a[0][2] * v.z),
                q12(add_y + a[1][0] * v.x + a[1][1] * v.y + a[1][2] * v.z),
                q12(add_z + a[2][0] * v.x + a[2][1] * v.y + a[2][2] * v.z)};
    out.saturated = saturate16(out.full, clamp_to_zero);
    return out;
}

ColorOut dpcs(const Rgb& color, std::int16_t cue, const Vec3i& far_color) {
    return finish_color(blend(color.r << 16, far_color.x, cue), blend(color.g << 16, far_color.y, cue),
                        blend(color.b << 16, far_color.z, cue), false);
}

ColorOut dpcl(const Rgb& color, const Vec3s& intensity, std::int16_t cue, const Vec3i& far_color) {
    return finish_color(blend((color.r << 4) * intensity.x, far_color.x, cue),
                        blend((color.g << 4) * intensity.y, far_color.y, cue),
                        blend((color.b << 4) * intensity.z, far_color.z, cue), false);
}

ColorOut gpf(const Vec3s& intensity, std::int16_t scale) {
    return finish_color(q12(scale * intensity.x), q12(scale * intensity.y), q12(scale * intensity.z), false);
}

ColorOut normal_color(const Matrix3& light, const Matrix3& color_matrix, const Vec3i& background, const Vec3s& normal,
                      bool clamp_to_zero) {
    const MatVec lit = matrix_vector(light, normal, Offset::None, {}, clamp_to_zero);
    const MatVec lit_color = matrix_vector(color_matrix, lit.saturated, Offset::Translation, background, clamp_to_zero);
    return finish_color(lit_color.full.x, lit_color.full.y, lit_color.full.z, clamp_to_zero);
}

// ---- Matrix functions ----

namespace {

std::int32_t wrap_add32(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

// A matrix-vector product of a vector cut to 16 bits (sign kept), shifted back by 12 or not.
Vec3i rotate_wide(const Matrix3& m, const Vec3i& v, bool shift) {
    const auto& a = m.m;
    const std::int16_t vx = static_cast<std::int16_t>(v.x), vy = static_cast<std::int16_t>(v.y),
                       vz = static_cast<std::int16_t>(v.z);
    const auto row = [&](int r) {
        const i64 sum = static_cast<i64>(a[r][0]) * vx + static_cast<i64>(a[r][1]) * vy + static_cast<i64>(a[r][2]) * vz;
        return static_cast<std::int32_t>(shift ? sum >> 12 : sum);
    };
    return {row(0), row(1), row(2)};
}

// Splits a component into a signed 15-bit low part and the rest above it.
std::int32_t split_part(std::int32_t v, bool high) {
    if (v < 0) {
        return high ? -((-v) >> 15) : -((-v) & 0x7fff);
    }
    return high ? v >> 15 : v & 0x7fff;
}

}  // namespace

Matrix3 mul_matrix(const Matrix3& a, const Matrix3& b) {
    Matrix3 out;
    for (int col = 0; col < 3; ++col) {
        const Vec3s column{b.m[0][col], b.m[1][col], b.m[2][col]};
        const MatVec r = matrix_vector(a, column);
        out.m[0][col] = r.saturated.x;
        out.m[1][col] = r.saturated.y;
        out.m[2][col] = r.saturated.z;
    }
    return out;
}

Transform comp_matrix(const Transform& a, const Transform& b) {
    Transform out;
    out.rot = mul_matrix(a.rot, b.rot);
    const Vec3s v{static_cast<std::int16_t>(b.trans.x), static_cast<std::int16_t>(b.trans.y),
                  static_cast<std::int16_t>(b.trans.z)};
    const MatVec r = matrix_vector(a.rot, v);
    out.trans = {wrap_add32(r.full.x, a.trans.x), wrap_add32(r.full.y, a.trans.y), wrap_add32(r.full.z, a.trans.z)};
    return out;
}

Vec3i apply_matrix_lv(const Matrix3& m, const Vec3i& v) {
    const Vec3i lo{split_part(v.x, true), split_part(v.y, true), split_part(v.z, true)};
    const Vec3i hi{split_part(v.x, false), split_part(v.y, false), split_part(v.z, false)};
    const Vec3i lo_r = rotate_wide(m, lo, false);
    const Vec3i hi_r = rotate_wide(m, hi, true);
    return {wrap_add32(hi_r.x, static_cast<std::int32_t>(static_cast<std::uint32_t>(lo_r.x) << 3)),
            wrap_add32(hi_r.y, static_cast<std::int32_t>(static_cast<std::uint32_t>(lo_r.y) << 3)),
            wrap_add32(hi_r.z, static_cast<std::int32_t>(static_cast<std::uint32_t>(lo_r.z) << 3))};
}

Matrix3 scale_matrix(const Matrix3& m, const Vec3i& v) {
    Matrix3 out;
    const std::int32_t s[3] = {v.x, v.y, v.z};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            const std::uint32_t product = static_cast<std::uint32_t>(m.m[r][c]) * static_cast<std::uint32_t>(s[r]);
            out.m[r][c] = static_cast<std::int16_t>(static_cast<std::int32_t>(product) >> 12);
        }
    }
    return out;
}

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Trig {
    double sx, cx, sy, cy, sz, cz;
};

Trig trig_of(const Vec3s& rot) {
    const double rx = (static_cast<double>(rot.x) / 4096.0) * 2.0 * kPi;
    const double ry = (static_cast<double>(rot.y) / 4096.0) * 2.0 * kPi;
    const double rz = (static_cast<double>(rot.z) / 4096.0) * 2.0 * kPi;
    return {std::sin(rx), std::cos(rx), std::sin(ry), std::cos(ry), std::sin(rz), std::cos(rz)};
}

// C cast of a double to a 16-bit element, going through a 32-bit integer like the game's truncation.
std::int16_t q12_element(double v) { return static_cast<std::int16_t>(static_cast<std::int32_t>(v * 4096.0)); }

}  // namespace

Matrix3 rot_matrix_zxy_neg(const Vec3s& rot) {
    const Trig t = trig_of(rot);
    Matrix3 r;
    r.m[0][0] = q12_element(t.cy * t.cz + t.sy * t.sx * t.sz);
    r.m[0][1] = q12_element(-t.cy * t.sz + t.sy * t.sx * t.cz);
    r.m[0][2] = q12_element(t.sy * t.cx);
    r.m[1][0] = q12_element(t.cx * t.sz);
    r.m[1][1] = q12_element(t.cx * t.cz);
    r.m[1][2] = q12_element(-t.sx);
    r.m[2][0] = q12_element(-t.sy * t.cz + t.cy * t.sx * t.sz);
    r.m[2][1] = q12_element(t.sy * t.sz + t.cy * t.sx * t.cz);
    r.m[2][2] = q12_element(t.cy * t.cx);
    return r;
}

Matrix3 rot_matrix_xyz(const Vec3s& rot) {
    const Trig t = trig_of(rot);
    Matrix3 r;
    r.m[0][0] = q12_element(t.cy * t.cz);
    r.m[0][1] = q12_element(-t.cy * t.sz);
    r.m[0][2] = q12_element(t.sy);
    r.m[1][0] = q12_element(t.sx * t.sy * t.cz + t.cx * t.sz);
    r.m[1][1] = q12_element(-t.sx * t.sy * t.sz + t.cx * t.cz);
    r.m[1][2] = q12_element(-t.sx * t.cy);
    r.m[2][0] = q12_element(-t.cx * t.sy * t.cz + t.sx * t.sz);
    r.m[2][1] = q12_element(t.cx * t.sy * t.sz + t.sx * t.cz);
    r.m[2][2] = q12_element(t.cx * t.cy);
    return r;
}

Matrix3 rot_matrix_zxy(const Vec3s& rot) {
    const Trig t = trig_of(rot);
    Matrix3 r;
    r.m[0][0] = q12_element(t.cy * t.cz + t.sy * t.sx * t.sz);
    r.m[0][1] = q12_element(t.cx * t.sz);
    r.m[0][2] = q12_element(-t.sy * t.cz + t.cy * t.sx * t.sz);
    r.m[1][0] = q12_element(-t.cy * t.sz + t.sy * t.sx * t.cz);
    r.m[1][1] = q12_element(t.cx * t.cz);
    r.m[1][2] = q12_element(t.sy * t.sz + t.cy * t.sx * t.cz);
    r.m[2][0] = q12_element(t.sy * t.cx);
    r.m[2][1] = q12_element(-t.sx);
    r.m[2][2] = q12_element(t.cy * t.cx);
    return r;
}

Matrix3 rot_matrix_z(std::int32_t angle) {
    const double rad = (static_cast<double>(angle) / 4096.0) * 2.0 * kPi;
    const double s = std::sin(rad), c = std::cos(rad);
    Matrix3 r;
    r.m[0][0] = q12_element(c);
    r.m[0][1] = q12_element(-s);
    r.m[0][2] = 0;
    r.m[1][0] = q12_element(s);
    r.m[1][1] = q12_element(c);
    r.m[1][2] = 0;
    r.m[2][0] = 0;
    r.m[2][1] = 0;
    r.m[2][2] = 4096;
    return r;
}

Vec3i vector_normal(const Vec3i& v, std::int64_t* squared_length) {
    const std::int64_t sq = static_cast<std::int64_t>(v.x) * v.x + static_cast<std::int64_t>(v.y) * v.y +
                            static_cast<std::int64_t>(v.z) * v.z;
    if (squared_length != nullptr) {
        *squared_length = sq;
    }
    const std::int32_t mag = world::game_sqrt(static_cast<std::int32_t>(sq));
    if (mag == 0) {
        return {};
    }
    return {static_cast<std::int32_t>((static_cast<std::int64_t>(v.x) << 12) / mag),
            static_cast<std::int32_t>((static_cast<std::int64_t>(v.y) << 12) / mag),
            static_cast<std::int32_t>((static_cast<std::int64_t>(v.z) << 12) / mag)};
}

Vec3i outer_product12(const Vec3i& a, const Vec3i& b) {
    return {static_cast<std::int32_t>((static_cast<std::int64_t>(a.y) * b.z - static_cast<std::int64_t>(a.z) * b.y) >> 12),
            static_cast<std::int32_t>((static_cast<std::int64_t>(a.z) * b.x - static_cast<std::int64_t>(a.x) * b.z) >> 12),
            static_cast<std::int32_t>((static_cast<std::int64_t>(a.x) * b.y - static_cast<std::int64_t>(a.y) * b.x) >> 12)};
}

RelativeRotation relative_rotation(const Vec3i& from, const Vec3i& to) {
    using world::wrap_add;
    using world::wrap_mul;
    const Vec3i dir0 = vector_normal(from);
    const Vec3i dir1 = vector_normal(to);
    const Vec3i normal = vector_normal(outer_product12(dir0, dir1));
    RelativeRotation out;
    out.azimuth = (world::fixed_atan2(normal.y, normal.x) - 1024) & 4095;
    // The two results the angle is made of are stored as 16-bit values.
    const std::int16_t cos_theta = static_cast<std::int16_t>(
        wrap_add(wrap_add(wrap_mul(dir0.x, dir1.x), wrap_mul(dir0.y, dir1.y)), wrap_mul(dir0.z, dir1.z)) >> 12);
    const Vec3i side = outer_product12(normal, dir0);
    const std::int16_t sin_theta = static_cast<std::int16_t>(
        wrap_add(wrap_add(wrap_mul(dir1.x, side.x), wrap_mul(dir1.y, side.y)), wrap_mul(dir1.z, side.z)) >> 12);
    out.altitude = world::fixed_atan2(sin_theta, cos_theta) & 4095;
    return out;
}

}  // namespace sh::math
