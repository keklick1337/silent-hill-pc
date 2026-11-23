#include "render/post/hdr.h"

#include <algorithm>
#include <cmath>

namespace sh::post {

namespace {

// The knee of the scene shader's soft_highlights (prim_batch.frag): below it nothing changes.
constexpr float kSoftKnee = 0.85f;
constexpr float kReinhardWhite = 4.f;  // linear light, relative to SDR white

}  // namespace

HdrSettings& hdr() {
    static HdrSettings s;
    return s;
}

HdrTonemap tonemapOf(const HdrSettings& s) {
    return s.tonemap > 0 && s.tonemap < kHdrTonemapCount ? HdrTonemap(s.tonemap) : HdrTonemap::Clamp;
}

bool sceneRollsOffHighlights(bool softHighlights, const HdrSettings& s, bool hdrFrameActive) {
    return softHighlights && !(hdrFrameActive && tonemapOf(s) != HdrTonemap::Clamp);
}

int bloomLevels(int width, int height) {
    int n = 0;
    int w = width / 2, h = height / 2;
    while (n < kMaxBloomLevels && std::min(w, h) >= 8) {
        ++n;
        w /= 2;
        h /= 2;
    }
    return n;
}

HdrResolvePlan planResolve(const HdrSettings& s, bool sceneFrame, bool hdr10, int width, int height) {
    HdrResolvePlan p;
    p.white = hdr10 ? hdr10White(s.paper_white, s.peak) : 1.f;
    if (sceneFrame) {
        p.tonemap = tonemapOf(s);
        p.bloom = std::clamp(s.bloom, 0.f, 1.f);
        if (p.bloom > 0.f) p.bloomLevels = bloomLevels(width, height);
        if (p.bloomLevels == 0) p.bloom = 0.f;
    }
    // HDR10 always runs it: the values above SDR white are kept up to the peak, not clipped by the present blit.
    p.run = hdr10 || p.tonemap != HdrTonemap::Clamp || p.bloom > 0.f;
    return p;
}

float srgbEncode(float l) {
    if (l <= 0.f) return 0.f;
    return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.f / 2.4f) - 0.055f;
}

float srgbDecode(float e) {
    if (e <= 0.f) return 0.f;
    return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
}

float hdr10White(float paperWhiteNits, float peakNits) {
    const float paper = std::max(paperWhiteNits, 1.f);
    return srgbEncode(std::max(peakNits, paper) / paper);
}

float pqEncode(float nits) {
    constexpr float m1 = 2610.f / 16384.f, m2 = 2523.f / 4096.f * 128.f;
    constexpr float c1 = 3424.f / 4096.f, c2 = 2413.f / 4096.f * 32.f, c3 = 2392.f / 4096.f * 32.f;
    const float y = std::pow(std::clamp(nits / 10000.f, 0.f, 1.f), m1);
    return std::pow((c1 + c2 * y) / (1.f + c3 * y), m2);
}

std::array<float, 3> tonemapRgb(std::array<float, 3> c, HdrTonemap op, float white) {
    for (float& v : c) v = std::max(v, 0.f);
    switch (op) {
        case HdrTonemap::Clamp:
            break;
        case HdrTonemap::Soft: {
            const float knee = kSoftKnee * white, room = white - knee;
            const float pk = std::max(c[0], std::max(c[1], c[2]));
            if (pk > knee) {
                const float k = (knee + room * (1.f - std::exp((knee - pk) / room))) / pk;
                for (float& v : c) v *= k;
            }
            break;
        }
        case HdrTonemap::Aces:
            for (float& v : c) {
                const float x = srgbDecode(v) * 0.6f;
                v = srgbEncode(std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.f, 1.f));
            }
            break;
        case HdrTonemap::Reinhard: {
            std::array<float, 3> l{srgbDecode(c[0]), srgbDecode(c[1]), srgbDecode(c[2])};
            const float y = 0.2126f * l[0] + 0.7152f * l[1] + 0.0722f * l[2];
            if (y > 0.f) {
                const float k = (1.f + y / (kReinhardWhite * kReinhardWhite)) / (1.f + y);
                for (int i = 0; i < 3; ++i) c[i] = srgbEncode(l[i] * k);
            }
            break;
        }
    }
    for (float& v : c) v = std::min(v, white);
    return c;
}

std::string_view hdrFrameUnavailable(bool nativeVulkan, bool floatTargets) {
    if (!nativeVulkan) return "needs the Vulkan renderer";
    if (!floatTargets) return "the GPU has no float render targets";
    return {};
}

}  // namespace sh::post
