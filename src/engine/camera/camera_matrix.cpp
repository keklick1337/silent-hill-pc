#include "engine/camera/camera_matrix.h"

#include <cmath>

#include "engine/camera/camera_math.h"
#include "engine/world/fixed_math.h"

namespace sh::camera {

namespace {

constexpr double kPi = 3.14159265358979323846;

double radians(std::int16_t angle) { return ((static_cast<double>(angle) / 4096.0) * 2.0) * kPi; }

std::int16_t q12_entry(double value) { return static_cast<std::int16_t>(value * 4096.0); }

std::int16_t saturate16(std::int64_t value) {
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return static_cast<std::int16_t>(value);
}

std::int32_t square_root(std::int32_t value) { return world::game_sqrt(value); }

}  // namespace

Matrix rotation_yxz(const Angles& angles) {
    const double rx = radians(angles.x);
    const double ry = radians(angles.y);
    const double rz = radians(angles.z);
    const double sx = std::sin(rx);
    const double cx = std::cos(rx);
    const double sy = std::sin(ry);
    const double cy = std::cos(ry);
    const double sz = std::sin(rz);
    const double cz = std::cos(rz);

    Matrix m;
    m.m[0][0] = q12_entry(cy * cz + sy * sx * sz);
    m.m[0][1] = q12_entry(-cy * sz + sy * sx * cz);
    m.m[0][2] = q12_entry(sy * cx);
    m.m[1][0] = q12_entry(cx * sz);
    m.m[1][1] = q12_entry(cx * cz);
    m.m[1][2] = q12_entry(-sx);
    m.m[2][0] = q12_entry(-sy * cz + cy * sx * sz);
    m.m[2][1] = q12_entry(sy * sz + cy * sx * cz);
    m.m[2][2] = q12_entry(cy * cx);
    return m;
}

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::int64_t sum = 0;
            for (int k = 0; k < 3; ++k) {
                sum += static_cast<std::int64_t>(a.m[i][k]) * b.m[k][j];
            }
            out.m[i][j] = saturate16(sum >> 12);
        }
    }
    return out;
}

Matrix transpose(const Matrix& m) {
    Matrix out = m;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out.m[i][j] = m.m[j][i];
        }
    }
    return out;
}

Angles apply(const Matrix& m, const Angles& v) {
    const std::int32_t in[3] = {v.x, v.y, v.z};
    std::int16_t out[3];
    for (int i = 0; i < 3; ++i) {
        std::int64_t sum = 0;
        for (int k = 0; k < 3; ++k) {
            sum += static_cast<std::int64_t>(m.m[i][k]) * in[k];
        }
        out[i] = saturate16(sum >> 12);
    }
    return Angles{out[0], out[1], out[2]};
}

Angles matrix_angles(const Matrix& m) {
    const std::int32_t planar = square_root(fx::wrap_mul(m.m[0][2], m.m[0][2]) + fx::wrap_mul(m.m[2][2], m.m[2][2]));
    Angles a;
    a.x = static_cast<std::int16_t>(fx::atan2(-m.m[1][2], planar));
    if (a.x == fx::angle(90.0f)) {
        a.z = 0;
        a.y = static_cast<std::int16_t>(fx::atan2(m.m[0][1], m.m[2][1]));
    } else if (a.x == fx::angle(-90.0f)) {
        a.z = 0;
        a.y = static_cast<std::int16_t>(fx::atan2(-m.m[0][1], -m.m[2][1]));
    } else {
        a.z = static_cast<std::int16_t>(fx::atan2(m.m[1][0], m.m[1][1]));
        a.y = static_cast<std::int16_t>(fx::atan2(m.m[0][2], m.m[2][2]));
    }
    return a;
}

Angles vector_angles(const Angles& v) {
    const std::int32_t xx = fx::wrap_mul(v.x, v.x);
    const std::int32_t zz = fx::wrap_mul(v.z, v.z);
    Angles a;
    a.x = static_cast<std::int16_t>(fx::atan2(-v.y, square_root(xx + zz)));
    a.y = static_cast<std::int16_t>(fx::atan2(v.x, v.z));
    a.z = 0;
    return a;
}

}  // namespace sh::camera
