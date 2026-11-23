// HDR frame passes (RENDER.md section 7.10): bloom and the tonemapper between the HDR scene
// target and the post chain, and the HDR10 encode of the window picture. The decisions are render/post/hdr.h.
//
// The contract other passes plug into: with hdr_frame on, Targets::scene_color() is RGBA16F and holds the frame in the
// console's encoding (1.0 = SDR white) with nothing clipped. A pass that adds light (ambient occlusion, volumetric
// fog, reflections) draws into it before PostPass::present; resolve() is the only place that maps it to the display.
// Its output, or the scene target itself when there is nothing to map, is what the post chain reads.
#pragma once

#include "render/post/hdr.h"
#include "render/rhi/rhi.h"

#include <array>

namespace sh::gpu {

class Renderer;

class HdrPass {
public:
    explicit HdrPass(Renderer& owner);
    ~HdrPass();
    HdrPass(const HdrPass&) = delete;
    HdrPass& operator=(const HdrPass&) = delete;

    // Bloom and tonemapping of `scene` (w x h, scene-oriented) by `plan`, outside a render pass. Returns the texture
    // the post chain reads: its own output (RGBA8, RGBA16F when plan.white > 1), or `scene` when the plan does not
    // run or a resource failed.
    rhi::TextureH resolve(rhi::CommandList& cmd, rhi::TextureH scene, int w, int h, const post::HdrResolvePlan& plan,
                          float threshold);

    // HDR10: an RGBA16F stand-in for the swapchain image (w x h) that the present and the overlay draw into, and its
    // PQ encode into the real swapchain image.
    rhi::TextureH compose_target(int w, int h);
    void encode(rhi::CommandList& cmd, rhi::TextureH compose, rhi::TextureH swap, int w, int h, float paperWhiteNits,
                float peakNits);

private:
    struct Target {
        rhi::TextureH texture;
        int w = 0, h = 0;
        rhi::TextureFormat format = rhi::TextureFormat::RGBA8;
    };
    bool ensure_pipelines();
    bool ensure(Target& t, int w, int h, rhi::TextureFormat format, const char* name);
    void fullscreen(rhi::RenderPass& pass, rhi::PipelineH pipe, int w, int h);
    void release();

    Renderer& owner_;
    bool failed_ = false;
    rhi::ShaderH vs_, bloom_fs_, resolve_fs_, encode_fs_;
    rhi::PipelineH bloom_pipe_, bloom_add_pipe_, resolve_pipe_, encode_pipe_;
    rhi::SamplerH linear_;
    rhi::BufferH triangle_;
    std::array<Target, post::kMaxBloomLevels> bloom_{};
    Target out_, compose_;
};

}  // namespace sh::gpu
