// SPDX-License-Identifier: GPL-3.0-or-later
//
// Temporal anti-aliasing, temporal upscaling and the spatial upscale of the native Vulkan backend (RENDER.md
// section 7.13, RENDER.md section 7.13): the settings, the jitter sequence, the render size of a quality mode and
// the decision when the history no longer belongs to the picture. The passes are render/gpu/temporal_pass.h and
// render/rhi/vk_temporal.h.
#pragma once

#include <array>

namespace sh::post {

enum class Upscaler : int {
    Off = 0,
    Taa = 1,      // temporal anti-aliasing at the full resolution
    Taau = 2,     // the world drawn smaller, reconstructed to the full resolution from the jittered frames
    Spatial = 3,  // the world drawn smaller, scaled up in one frame and sharpened
};
constexpr int kUpscalerCount = 4;

enum class UpscaleQuality : int {
    Quality = 0,      // 2/3 of the size per axis
    Balanced = 1,     // 0.58
    Performance = 2,  // 1/2
    Ultra = 3,        // 1/3
};
constexpr int kUpscaleQualityCount = 4;

struct TemporalSettings {
    int upscaler = 0;         // upscaler (Upscaler)
    int quality = 0;          // upscale_quality (UpscaleQuality)
    float sharpness = 0.25f;  // upscale_sharpness: 0 = none
    int debug = 0;            // temporal_debug: 1 shows the motion vectors instead of the picture
    // Frame pacing of the native Vulkan swapchain.
    int present_mode = 0;      // present_mode: 0 auto (vsync: FIFO, else immediate or mailbox), 1 FIFO,
                               // 2 mailbox, 3 immediate, 4 FIFO relaxed
    int frames_in_flight = 2;  // frames_in_flight: 1 or 2 frames the CPU may record ahead of the GPU
    int present_wait = 0;      // present_wait: wait until the frame before is on screen (VK_KHR_present_wait)
};

TemporalSettings& temporal();

Upscaler upscalerOf(const TemporalSettings& s);

// Share of the output size per axis the world is drawn at (1 for Off and TAA).
float renderScale(Upscaler u, UpscaleQuality q);

// The i-th element (i >= 1) of the Halton sequence of `base`, in [0, 1).
float halton(int index, int base);

// Jitter phases: 8 at full size, more as the render size shrinks, so every output pixel is hit by a sample.
int jitterPhases(float scale);

// Sub-pixel offset of frame `frame` in render pixels, each axis in [-0.5, 0.5): Halton (2, 3).
std::array<float, 2> jitterOffset(int frame, int phases);

// A camera between two drawn frames (view = rot * world + trans, as scene::Camera) and its projection distance.
struct CameraPose {
    std::array<double, 9> rot{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<double, 3> trans{0, 0, 0};
    float h = 0;
};

// The two frames do not show one continuous view (a cut): the eye jumped further than `move` units, turned by more
// than acos(`turn_cos`) or changed its projection distance. The history is then dropped.
bool cameraCut(const CameraPose& from, const CameraPose& to, double move = 512.0, double turn_cos = 0.9);

// prev_view = m * view + c of a point that stands still in the world, from the camera of this frame to the one of
// the frame before: rows of a 3 x 4 matrix (the fourth column is c).
std::array<float, 12> reprojection(const CameraPose& now, const CameraPose& before);

}  // namespace sh::post
