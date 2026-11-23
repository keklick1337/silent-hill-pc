#pragma once

#include "formats/convert/anim/anm_file.h"

#include <algorithm>
#include <array>
#include <cmath>

// Internal to sh_convert_model: a game bone pose (Q12 matrix, PS1 units, +Y down) as glTF transforms.
namespace sh::convert::model::skin {

using Mat3 = std::array<double, 9>;   // row-major
using Mat4 = std::array<float, 16>;   // column-major, as glTF stores it

struct Affine {
    Mat3 m{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<double, 3> t{0, 0, 0};
};

// The model converter's axis change (model_converter.h): x' = -x/256, y' = -y/256, z' = z/256, i.e. G = D P D with
// D = diag(-1, -1, 1) and translation in metres.
inline Affine to_gltf(const anim::Pose& p) {
    static constexpr double d[3] = {-1, -1, 1};
    Affine a;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            a.m[r * 3 + c] = d[r] * d[c] * p.m[static_cast<std::size_t>(r * 3 + c)] / 4096.0;
        }
        a.t[static_cast<std::size_t>(r)] = d[r] * p.t[static_cast<std::size_t>(r)] / 256.0;
    }
    return a;
}

inline double det3(const Mat3& m) {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
}

inline Mat3 inverse3(const Mat3& m) {
    const double det = det3(m);
    const double k = std::abs(det) < 1e-12 ? 0.0 : 1.0 / det;
    return {(m[4] * m[8] - m[5] * m[7]) * k, (m[2] * m[7] - m[1] * m[8]) * k, (m[1] * m[5] - m[2] * m[4]) * k,
            (m[5] * m[6] - m[3] * m[8]) * k, (m[0] * m[8] - m[2] * m[6]) * k, (m[2] * m[3] - m[0] * m[5]) * k,
            (m[3] * m[7] - m[4] * m[6]) * k, (m[1] * m[6] - m[0] * m[7]) * k, (m[0] * m[4] - m[1] * m[3]) * k};
}

inline Mat4 inverse_column_major(const Affine& a) {
    const Mat3 inv = inverse3(a.m);
    Mat4 out{};
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            out[static_cast<std::size_t>(c * 4 + r)] = static_cast<float>(inv[static_cast<std::size_t>(r * 3 + c)]);
        }
        double t = 0;
        for (int k = 0; k < 3; ++k) {
            t -= inv[static_cast<std::size_t>(r * 3 + k)] * a.t[static_cast<std::size_t>(k)];
        }
        out[static_cast<std::size_t>(12 + r)] = static_cast<float>(t);
    }
    out[15] = 1.0f;
    return out;
}

struct Trs {
    std::array<float, 3> translation{0, 0, 0};
    std::array<float, 4> rotation{0, 0, 0, 1};  // xyzw
    std::array<float, 3> scale{1, 1, 1};
};

// The game's matrices are s8 * 32 (Q12): rotations with about 1% noise, not orthonormal. glTF needs T * R * S, so
// the matrix is split by its polar decomposition M = R * P (R the nearest rotation) and S is the diagonal of P; the
// off-diagonal rest of P (shear, below the quantisation step) is dropped.
inline Trs to_trs(const Affine& a) {
    Trs out;
    for (std::size_t i = 0; i < 3; ++i) {
        out.translation[i] = static_cast<float>(a.t[i]);
    }
    Mat3 r = a.m;
    if (std::abs(det3(r)) < 1e-9) {
        return out;
    }
    for (int it = 0; it < 30; ++it) {
        const Mat3 inv = inverse3(r);
        Mat3 next;
        double change = 0;
        for (std::size_t i = 0; i < 3; ++i) {
            for (std::size_t j = 0; j < 3; ++j) {
                next[i * 3 + j] = 0.5 * (r[i * 3 + j] + inv[j * 3 + i]);  // (R + R^-T) / 2
                change = std::max(change, std::abs(next[i * 3 + j] - r[i * 3 + j]));
            }
        }
        r = next;
        if (change < 1e-12) {
            break;
        }
    }
    // P = R^T M; its diagonal is the scale along the rotated axes.
    for (std::size_t i = 0; i < 3; ++i) {
        double s = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            s += r[k * 3 + i] * a.m[k * 3 + i];
        }
        out.scale[i] = static_cast<float>(s);
    }
    // Rotation matrix to quaternion (Shepperd).
    const double tr = r[0] + r[4] + r[8];
    double x, y, z, w;
    if (tr > 0) {
        const double s = std::sqrt(tr + 1.0) * 2;
        w = 0.25 * s;
        x = (r[7] - r[5]) / s;
        y = (r[2] - r[6]) / s;
        z = (r[3] - r[1]) / s;
    } else if (r[0] > r[4] && r[0] > r[8]) {
        const double s = std::sqrt(1.0 + r[0] - r[4] - r[8]) * 2;
        w = (r[7] - r[5]) / s;
        x = 0.25 * s;
        y = (r[1] + r[3]) / s;
        z = (r[2] + r[6]) / s;
    } else if (r[4] > r[8]) {
        const double s = std::sqrt(1.0 + r[4] - r[0] - r[8]) * 2;
        w = (r[2] - r[6]) / s;
        x = (r[1] + r[3]) / s;
        y = 0.25 * s;
        z = (r[5] + r[7]) / s;
    } else {
        const double s = std::sqrt(1.0 + r[8] - r[0] - r[4]) * 2;
        w = (r[3] - r[1]) / s;
        x = (r[2] + r[6]) / s;
        y = (r[5] + r[7]) / s;
        z = 0.25 * s;
    }
    const double n = std::sqrt(x * x + y * y + z * z + w * w);
    out.rotation = {static_cast<float>(x / n), static_cast<float>(y / n), static_cast<float>(z / n),
                    static_cast<float>(w / n)};
    return out;
}

}  // namespace sh::convert::model::skin
