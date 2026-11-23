// SPDX-License-Identifier: GPL-3.0-or-later

#include <cmath>
#include <set>
#include <utility>

#include "framework/sh_test.h"
#include "render/post/temporal.h"

namespace {

using sh::post::CameraPose;
using sh::post::UpscaleQuality;
using sh::post::Upscaler;

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

}  // namespace

SH_TEST(temporal_off_by_default) {
    const sh::post::TemporalSettings s;
    SH_CHECK(sh::post::upscalerOf(s) == Upscaler::Off);
    SH_CHECK(s.present_mode == 0 && s.frames_in_flight == 2 && s.present_wait == 0);
    SH_CHECK(sh::post::renderScale(Upscaler::Off, UpscaleQuality::Performance) == 1.0f);
    SH_CHECK(sh::post::renderScale(Upscaler::Taa, UpscaleQuality::Ultra) == 1.0f);
}

SH_TEST(temporal_quality_scales) {
    SH_CHECK(near(sh::post::renderScale(Upscaler::Taau, UpscaleQuality::Quality), 2.0f / 3.0f));
    SH_CHECK(near(sh::post::renderScale(Upscaler::Spatial, UpscaleQuality::Performance), 0.5f));
    SH_CHECK(sh::post::renderScale(Upscaler::Taau, UpscaleQuality::Balanced) <
             sh::post::renderScale(Upscaler::Taau, UpscaleQuality::Quality));
}

SH_TEST(temporal_halton_jitter) {
    SH_CHECK(near(sh::post::halton(1, 2), 0.5f));
    SH_CHECK(near(sh::post::halton(2, 3), 2.0f / 3.0f));
    SH_CHECK(near(sh::post::halton(3, 2), 0.75f));
    SH_CHECK(sh::post::jitterPhases(1.0f) == 8);
    SH_CHECK(sh::post::jitterPhases(0.5f) == 32);
    std::set<std::pair<float, float>> seen;
    for (int f = 0; f < 8; ++f) {
        const auto j = sh::post::jitterOffset(f, 8);
        SH_CHECK(j[0] >= -0.5f && j[0] < 0.5f && j[1] >= -0.5f && j[1] < 0.5f);
        seen.insert({j[0], j[1]});
    }
    SH_CHECK(seen.size() == 8);
    const auto a = sh::post::jitterOffset(3, 8), b = sh::post::jitterOffset(11, 8);
    SH_CHECK(a == b);
}

SH_TEST(temporal_camera_cut) {
    CameraPose a;
    a.h = 256;
    CameraPose b = a;
    b.trans = {100, 0, 0};
    SH_CHECK(!sh::post::cameraCut(a, b));
    b.trans = {0, 0, 2000};
    SH_CHECK(sh::post::cameraCut(a, b));
    b = a;
    b.rot = {0, 0, 1, 0, 1, 0, -1, 0, 0};  // a quarter turn
    SH_CHECK(sh::post::cameraCut(a, b));
    b = a;
    b.h = 300;
    SH_CHECK(sh::post::cameraCut(a, b));
}

SH_TEST(temporal_reprojection_of_a_still_point) {
    // A point at world w seen by two cameras: the matrix takes its view position in `now` to the one in `before`.
    CameraPose now, before;
    const double c = std::cos(0.1), s = std::sin(0.1);
    now.rot = {c, 0, -s, 0, 1, 0, s, 0, c};
    now.trans = {10, -20, 300};
    before.trans = {15, -20, 290};
    const double w[3] = {50, 70, 900};
    double vn[3], vb[3];
    for (int r = 0; r < 3; ++r) {
        vn[r] = now.rot[r * 3] * w[0] + now.rot[r * 3 + 1] * w[1] + now.rot[r * 3 + 2] * w[2] + now.trans[r];
        vb[r] =
            before.rot[r * 3] * w[0] + before.rot[r * 3 + 1] * w[1] + before.rot[r * 3 + 2] * w[2] + before.trans[r];
    }
    const auto m = sh::post::reprojection(now, before);
    for (int r = 0; r < 3; ++r) {
        const double p = m[r * 4] * vn[0] + m[r * 4 + 1] * vn[1] + m[r * 4 + 2] * vn[2] + m[r * 4 + 3];
        SH_CHECK(std::fabs(p - vb[r]) < 1e-3);
    }
}
