#pragma once

// HDR frame, tonemapper, bloom and HDR10 output (RENDER.md section 7.10) as plain C++ with
// no device: the live settings and the decisions and maths the passes of render/gpu/hdr_pass.cpp record. Only the
// native Vulkan renderer has float targets (rhi::Caps::floatTargets); elsewhere all of it is off.
//
// The values of the HDR frame are those of the 8-bit frame (the console's colour, sRGB-encoded, 1.0 = white), only
// not clipped: the per-pixel flashlight and additive blending may pass 1.0. The tonemapper takes them back into the
// range of the display; with tonemap "clamp" and no bloom the picture is the 8-bit one.

#include <array>
#include <string_view>

namespace sh::post {

enum class HdrTonemap : int {
    Clamp = 0,     // clipped at white, as the 8-bit frame (soft_highlights still rolls the beam off in the shader)
    Soft = 1,      // soft_highlights as an operator of the whole frame: linear to a knee, a smooth shoulder to white
    Aces = 2,      // the ACES filmic fit on linear light
    Reinhard = 3,  // extended Reinhard on luminance, white at 4x the SDR white
};
constexpr int kHdrTonemapCount = 4;

struct HdrSettings {
    bool frame = false;           // hdr_frame: the scene target is RGBA16F
    int tonemap = 0;              // hdr_tonemap (HdrTonemap)
    float bloom = 0.f;            // bloom: the share of the light above the threshold that spreads (0 = off)
    float bloom_threshold = 1.f;  // bloom_threshold: only light above this level blooms (1.0 = above white)
    bool output = false;          // hdr_output: HDR10 swapchain when the display offers one
    float paper_white = 200.f;    // hdr_paper_white: nits of SDR white in HDR10
    float peak = 1000.f;          // hdr_peak: the brightest nits the tonemapper sends in HDR10
};

// The live settings (config keys above, the options menu); read by the renderer every frame.
HdrSettings& hdr();

HdrTonemap tonemapOf(const HdrSettings& s);

// The scene shader's own highlight roll-off (soft_highlights) stays unless an operator of the HDR frame replaces it.
bool sceneRollsOffHighlights(bool softHighlights, const HdrSettings& s, bool hdrFrameActive);

// What the resolve of the HDR frame does this frame. Menus, movies and every frame without the 3D world are only
// clipped, so they look exactly as on the 8-bit frame.
struct HdrResolvePlan {
    bool run = false;  // false: the HDR frame goes on as it is (the post pass or the present blit clips it)
    HdrTonemap tonemap = HdrTonemap::Clamp;
    float bloom = 0.f;
    int bloomLevels = 0;
    float white = 1.f;  // the largest value written: 1, or the HDR10 peak in the frame's encoding
};
HdrResolvePlan planResolve(const HdrSettings& s, bool sceneFrame, bool hdr10, int width, int height);

// Mip chain of the bloom: the first level at half size, each next one half again, down to 8 texels on the short side;
// at most kMaxBloomLevels.
constexpr int kMaxBloomLevels = 6;
int bloomLevels(int width, int height);

// Phones (Sh_IsMobile) run a shorter chain: each level is a render pass, and on a tiler every pass boundary writes its
// target out to memory. The levels past kMobileBloomLevels are the widest, faintest haze (tens of texels of the
// screen each); their share of the light goes to the last level kept, so the narrower glow keeps its strength and
// the sum of light stays the same. `lastWeight` is that last level's share in levels (1 when nothing was left out).
constexpr int kMobileBloomLevels = 5;
struct BloomChain {
    int levels = 0;
    float lastWeight = 1.f;
};
BloomChain mobileBloomChain(int levels);

// The frame's encoding (sRGB curve, 1.0 = SDR white) against linear light relative to SDR white, both extended past
// 1.0 by the same curve.
float srgbEncode(float linear);
float srgbDecode(float encoded);

// HDR10: the white the tonemapper aims at, in the frame's encoding (peak / paper white as linear light).
float hdr10White(float paperWhiteNits, float peakNits);
// SMPTE ST 2084 (PQ) of an absolute luminance in nits, 0..1.
float pqEncode(float nits);

// The operators of shaders/hdr_resolve.frag, for tests and calibration: one RGB value in the frame's encoding.
std::array<float, 3> tonemapRgb(std::array<float, 3> c, HdrTonemap op, float white);

// Why the HDR frame is not available on this device, or "" when it is.
std::string_view hdrFrameUnavailable(bool nativeVulkan, bool floatTargets);

}  // namespace sh::post
