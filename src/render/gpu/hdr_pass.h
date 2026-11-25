// HDR frame passes (RENDER.md section 7.10): bloom and the tonemapper between the HDR scene
// target and the post chain, and the HDR10 encode of the window picture. The decisions are render/post/hdr.h.
//
// The contract other passes plug into: with hdr_frame on, Targets::scene_color() is RGBA16F and holds the frame in the
// console's encoding (1.0 = SDR white) with nothing clipped. A pass that adds light (ambient occlusion, volumetric
// fog, reflections) draws into it before PostPass::present; resolve() is the only place that maps it to the display.
// Its output, or the scene target itself when there is nothing to map, is what the post chain reads.
#pragma once

#include "render/gpu/targets.h"
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
    // `fold`: the world's atmosphere layer still owed on `scene` (phones, Targets::world_layer), drawn in here.
    rhi::TextureH resolve(rhi::CommandList& cmd, rhi::TextureH scene, int w, int h, const post::HdrResolvePlan& plan,
                          float threshold, const Targets::WorldLayer* fold = nullptr);
    // Phones: the same resolve drawn straight into the window (`window`, the present's last step from `src` of the
    // scene to `dst`, the scene sampled with `scene_sampler`) in a render pass the overlay continues, so the
    // tonemapped frame is never written to memory and read back. False when it could not (nothing was drawn).
    bool present(rhi::CommandList& cmd, rhi::TextureH scene, int w, int h, const post::HdrResolvePlan& plan,
                 float threshold, rhi::TextureH window, int win_w, int win_h, const rhi::Rect& src,
                 const rhi::Rect& dst, bool covered, rhi::SamplerH scene_sampler,
                 const Targets::WorldLayer* fold = nullptr);

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
    // std140 ResolveFs of shaders/hdr_resolve.frag.
    struct alignas(16) ResolveUniforms {
        std::array<float, 4> mode{};    // x: operator, y: bloom share, z: threshold, w: flip v
        std::array<float, 4> out{};     // x: white, y: 1 / bloom levels, z: 1 = phone chain, w: weight of s_bloom2
        std::array<float, 4> texel{};   // xy: 1 / size of s_bloom2, zw: 1 / size of the first bloom level
        std::array<float, 4> src{};     // drawn into the window (present): the source rect, as PresentFs
        std::array<float, 4> dst{};     // and the present rect; w 0: a pass of the scene's size
        std::array<float, 4> win{};     // xy: the window's size (a turned swapchain image, rhi_vulkan.cpp)
        std::array<float, 4> layer{};   // x: 1 = fold the atmosphere layer in, y: its step, z: view depth at depth 1
        std::array<float, 4> world{};   // the world's viewport in the scene target
        std::array<float, 4> result{};  // x: 1 = the scene is the temporal result, y: its sharpening
    };
    static_assert(sizeof(ResolveUniforms) == 144);
    // The bloom chain's result: what the resolve samples and its uniforms.
    struct Bloomed {
        ResolveUniforms u;
        rhi::TextureH bloom, bloom2;
    };
    Bloomed bloom(rhi::CommandList& cmd, rhi::TextureH scene, int w, int h, const post::HdrResolvePlan& plan,
                  float threshold);
    void draw_resolve(rhi::RenderPass& pass, rhi::TextureH scene, Bloomed& b, rhi::SamplerH scene_sampler,
                      const Targets::WorldLayer* fold);
    bool ensure_pipelines();
    bool ensure(Target& t, int w, int h, rhi::TextureFormat format, const char* name);
    void fullscreen(rhi::RenderPass& pass, rhi::PipelineH pipe, int w, int h);
    void release();

    Renderer& owner_;
    bool failed_ = false;
    rhi::ShaderH vs_, bloom_fs_, bloom_up_fs_, resolve_fs_, encode_fs_;
    rhi::PipelineH bloom_pipe_, bloom_add_pipe_, bloom_up_pipe_, resolve_pipe_, encode_pipe_;
    rhi::SamplerH linear_, nearest_;
    rhi::BufferH triangle_;
    std::array<Target, post::kMaxBloomLevels> bloom_{};
    // Phones: the upsampled levels, apart from the downsampled ones so that no pass loads its target.
    std::array<Target, post::kMaxBloomLevels> bloom_up_{};
    bool mobile_ = false;
    Target out_, compose_;
};

}  // namespace sh::gpu
