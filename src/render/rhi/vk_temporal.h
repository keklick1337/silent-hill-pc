// SPDX-License-Identifier: GPL-3.0-or-later
//
// Temporal anti-aliasing (TAA), temporal upscaling (TAAU) and a spatial upscale of the native Vulkan backend
// (RENDER.md section 7.13). The world is drawn with a sub-pixel jitter into `src`
// of the scene target (smaller than `dst` when upscaling); the passes reconstruct `dst` from it and a history kept
// at the size of `dst`, and draw the result over `dst` of the same target before the screen overlay layer.
// Motion: a still point is reprojected from its depth with the two cameras; posed meshes and moving objects are
// drawn into a motion buffer from their two positions (`dynamic`), and only where they are what the scene shows.
#pragma once

#include "render/rhi/rhi.h"

#include <array>
#include <memory>
#include <span>
#include <string>

namespace sh::rhi {

// One vertex of the motion triangles: the jittered clip position the scene drew it at (z in 0..1 times w), the
// previous and the present unjittered clip x, y, w, and what it is.
struct TemporalVertex {
    float clip[4];
    float prev[3];
    float kind;  // 1 moved geometry, 2 geometry with no previous pose (no history there), 3 translucent
    float cur[3];
    float pad;
};
static_assert(sizeof(TemporalVertex) == 48);

enum class TemporalMode : int { Taa = 0, Spatial = 1, Motion = 2 };

struct TemporalInputs {
    TextureH color;  // the scene target
    TextureH depth;  // its depth, created sampled
    // Pixels of the target, top-left origin: what the world was drawn into, and what the result covers.
    std::array<float, 4> src{};
    std::array<float, 4> dst{};
    std::array<float, 4> proj{};        // view to NDC without the jitter: ndc = xy * (v.xy / v.z) + zw (NDC +Y up)
    std::array<float, 4> prev_proj{};   // the same of the frame before
    std::array<float, 2> jitter{};      // where the scene was shifted to, in `src` pixels (x right, y down)
    float sz_max = 65535;               // window depth 1 is this view depth
    std::array<float, 12> reproject{};  // rows of a 3 x 4 matrix: previous view position of a still point
    TemporalMode mode = TemporalMode::Taa;
    bool reset = false;                       // the history does not belong to this picture
    float sharpness = 0.0f;                   // 0..1
    std::span<const TemporalVertex> dynamic;  // triangle list
    // A tiler's result is left in its history image for the frame's resolve (result(), applyResult()) instead of
    // being drawn over `dst` of the scene target.
    bool fold = false;
};

class Temporal {
public:
    virtual ~Temporal() = default;
    // Records the passes into the frame being recorded, outside any render pass; the profiler lists them as
    // "temporal". An error leaves the target as it was.
    virtual Result<void> apply(const TemporalInputs& in) = 0;
    // Drops the history (the next apply starts from its own frame).
    virtual void reset() = 0;
    // After an apply with `fold`: the result as a sampled RHI texture (RGBA16F at `dst`'s size, colour in rgb), and
    // the pass that draws it over `dst` of the scene target as the apply would have (sharpened by `sharpness`),
    // for a frame whose resolve does not read it there. Until the next apply; applyResult() at most once.
    virtual TextureH result() const { return {}; }
    virtual Result<void> applyResult() { return Result<void>::fail("temporal: no result"); }
};

// Empty `reason` and a working object, or null and why not. `tiler`: the passes are laid out for a tile-based phone
// GPU (fragment passes into compressible attachments, a half-size motion buffer, no separate sharpening pass); the
// picture is the same within the precision of 16-bit floats.
std::unique_ptr<Temporal> createTemporal(Device& device, std::string* reason, bool tiler = false);

}  // namespace sh::rhi
