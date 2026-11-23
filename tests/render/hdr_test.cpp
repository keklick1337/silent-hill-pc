// sh::post HDR frame decisions (render/post/hdr.h): the resolve plan, the operators, the HDR10 encoding.

#include <cmath>

#include "framework/sh_test.h"
#include "render/post/hdr.h"

namespace {

using sh::post::HdrResolvePlan;
using sh::post::HdrSettings;
using sh::post::HdrTonemap;

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

}  // namespace

// Defaults reproduce the 8-bit frame: no pass runs, the shader keeps its own roll-off.
SH_TEST(hdr_neutral_defaults) {
    HdrSettings s;
    s.frame = true;
    const HdrResolvePlan p = sh::post::planResolve(s, true, false, 1280, 960);
    SH_CHECK(!p.run);
    SH_CHECK(p.white == 1.f);
    SH_CHECK(sh::post::sceneRollsOffHighlights(true, s, true));
    SH_CHECK(!sh::post::sceneRollsOffHighlights(false, s, true));
}

// Menus and movies (no 3D world) are never tonemapped nor bloomed.
SH_TEST(hdr_non_scene_frames_untouched) {
    HdrSettings s;
    s.frame = true;
    s.tonemap = int(HdrTonemap::Aces);
    s.bloom = 0.5f;
    const HdrResolvePlan p = sh::post::planResolve(s, false, false, 1280, 960);
    SH_CHECK(!p.run);
    const HdrResolvePlan q = sh::post::planResolve(s, true, false, 1280, 960);
    SH_CHECK(q.run && q.tonemap == HdrTonemap::Aces && q.bloomLevels == 6);
    SH_CHECK(!sh::post::sceneRollsOffHighlights(true, s, true));
    SH_CHECK(sh::post::sceneRollsOffHighlights(true, s, false));  // no HDR frame: the shader keeps it
}

SH_TEST(hdr_bloom_levels) {
    SH_CHECK(sh::post::bloomLevels(320, 240) == 4);  // 160x120 .. 20x15
    SH_CHECK(sh::post::bloomLevels(8, 8) == 0);
    SH_CHECK(sh::post::bloomLevels(3840, 2160) == sh::post::kMaxBloomLevels);
}

// Clamp is the 8-bit frame; Soft is identical below the knee and the shader's curve above it.
SH_TEST(hdr_operators) {
    auto c = sh::post::tonemapRgb({0.5f, 1.7f, -0.2f}, HdrTonemap::Clamp, 1.f);
    SH_CHECK(c[0] == 0.5f && c[1] == 1.f && c[2] == 0.f);
    c = sh::post::tonemapRgb({0.8f, 0.4f, 0.1f}, HdrTonemap::Soft, 1.f);
    SH_CHECK(c[0] == 0.8f && c[1] == 0.4f);
    c = sh::post::tonemapRgb({2.f, 1.f, 0.f}, HdrTonemap::Soft, 1.f);
    const float k = (0.85f + 0.15f * (1.f - std::exp((0.85f - 2.f) / 0.15f))) / 2.f;
    SH_CHECK(near(c[0], 2.f * k) && near(c[1], k) && c[0] < 1.f);
    for (HdrTonemap op : {HdrTonemap::Aces, HdrTonemap::Reinhard}) {
        const auto lo = sh::post::tonemapRgb({0.f, 0.f, 0.f}, op, 1.f);
        const auto hi = sh::post::tonemapRgb({8.f, 8.f, 8.f}, op, 1.f);
        SH_CHECK(lo[0] == 0.f && hi[0] <= 1.f && hi[0] > 0.8f);
    }
}

SH_TEST(hdr10_encoding) {
    SH_CHECK(near(sh::post::srgbDecode(sh::post::srgbEncode(0.37f)), 0.37f));
    SH_CHECK(near(sh::post::pqEncode(10000.f), 1.f));
    SH_CHECK(near(sh::post::pqEncode(100.f), 0.5081f, 1e-3f));  // ST 2084 reference
    SH_CHECK(near(sh::post::hdr10White(200.f, 200.f), 1.f));
    SH_CHECK(sh::post::hdr10White(200.f, 1000.f) > 1.f);
    HdrSettings s;
    s.frame = true;
    const HdrResolvePlan p = sh::post::planResolve(s, false, true, 1280, 960);
    SH_CHECK(p.run && p.white > 1.f);  // HDR10 always maps: nothing above white is clipped by the present blit
}
