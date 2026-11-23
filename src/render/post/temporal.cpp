// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/post/temporal.h"

#include <algorithm>
#include <cmath>

namespace sh::post {

TemporalSettings& temporal() {
    static TemporalSettings s;
    return s;
}

Upscaler upscalerOf(const TemporalSettings& s) {
    return static_cast<Upscaler>(std::clamp(s.upscaler, 0, kUpscalerCount - 1));
}

float renderScale(Upscaler u, UpscaleQuality q) {
    if (u != Upscaler::Taau && u != Upscaler::Spatial) return 1.0f;
    switch (q) {
        case UpscaleQuality::Quality:
            return 2.0f / 3.0f;
        case UpscaleQuality::Balanced:
            return 0.58f;
        case UpscaleQuality::Performance:
            return 0.5f;
        case UpscaleQuality::Ultra:
            return 1.0f / 3.0f;
    }
    return 1.0f;
}

float halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    for (int i = index; i > 0; i /= base) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
    }
    return r;
}

int jitterPhases(float scale) {
    const float s = std::clamp(scale, 0.1f, 1.0f);
    return std::clamp(static_cast<int>(std::ceil(8.0f / (s * s))), 8, 72);
}

std::array<float, 2> jitterOffset(int frame, int phases) {
    // Index 0 of the sequence is the origin of both axes; starting at 1 keeps every phase distinct.
    const int i = (phases > 0 ? ((frame % phases) + phases) % phases : 0) + 1;
    return {halton(i, 2) - 0.5f, halton(i, 3) - 0.5f};
}

namespace {

std::array<double, 3> eye(const CameraPose& c) {
    const auto& m = c.rot;
    const auto& t = c.trans;
    return {-(m[0] * t[0] + m[3] * t[1] + m[6] * t[2]), -(m[1] * t[0] + m[4] * t[1] + m[7] * t[2]),
            -(m[2] * t[0] + m[5] * t[1] + m[8] * t[2])};
}

}  // namespace

bool cameraCut(const CameraPose& from, const CameraPose& to, double move, double turn_cos) {
    const auto a = eye(from), b = eye(to);
    const double d =
        std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    double trace = 0;
    for (std::size_t i = 0; i < 9; ++i) trace += from.rot[i] * to.rot[i];
    return d > move || (trace - 1.0) * 0.5 < turn_cos || from.h != to.h;
}

std::array<float, 12> reprojection(const CameraPose& now, const CameraPose& before) {
    // prev = Rb * Rn^T * (v - tn) + tb.
    const auto& rn = now.rot;
    const auto& rb = before.rot;
    double m[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            m[r * 3 + c] =
                rb[r * 3 + 0] * rn[c * 3 + 0] + rb[r * 3 + 1] * rn[c * 3 + 1] + rb[r * 3 + 2] * rn[c * 3 + 2];
    std::array<float, 12> out{};
    for (int r = 0; r < 3; ++r) {
        const double c =
            before.trans[r] - (m[r * 3] * now.trans[0] + m[r * 3 + 1] * now.trans[1] + m[r * 3 + 2] * now.trans[2]);
        out[r * 4 + 0] = static_cast<float>(m[r * 3 + 0]);
        out[r * 4 + 1] = static_cast<float>(m[r * 3 + 1]);
        out[r * 4 + 2] = static_cast<float>(m[r * 3 + 2]);
        out[r * 4 + 3] = static_cast<float>(c);
    }
    return out;
}

}  // namespace sh::post
