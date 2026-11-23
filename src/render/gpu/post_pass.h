// Post-processing and present (RENDER.md section 3): the post chain of the preset over the scene target
// (src/render/post holds the settings and shader text), the blit to the swapchain into Targets::present_rect, the
// GPU timer, screenshots.
//
// The decisions (settings -> uniforms, scale mode -> blits) are plain functions so tests can check them without a
// device; PostPass only records them.
#pragma once

#include "render/post/post.h"
#include "render/rhi/rhi.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace sh::gpu {

class HdrPass;
class Renderer;

// Everything the post shader needs this frame, as the preset and the live config give it.
struct PostInputs {
    post::PostSettings settings;
    float post_intensity = 1.f;     // look filter mix, 0..1
    float tonemap_intensity = 1.f;  // tonemap mix, 0..1
    std::uint32_t frame = 0;        // post passes run so far: seeds the grain
    int width = 0, height = 0;      // scene target size
    // The scene target was rendered with its top row at texture v = 0 (SDL3 GPU); GL keeps v = 1 at the top.
    bool sample_flip_y = false;
    bool frag_coord_top_left = false;  // rhi::Caps::fragCoordTopLeft
};

// std140 block `PostFs` of shaders/post.frag. Integers travel as floats.
struct alignas(16) PostUniforms {
    std::array<float, 4> mode{};    // x: look filter, y: tonemap, z: fxaa, w: calibration bar
    std::array<float, 4> mix{};     // x: look intensity, y: tonemap intensity, z: grain time, w: fragcoord flip height
    std::array<float, 4> adjust{};  // x: brightness, y: contrast, z: saturation, w: 1 = sample at 1 - v
    std::array<float, 4> texel{};   // xy: 1 / source size, z: largest value written (0 = 1.0; HDR10 keeps more)
};
static_assert(sizeof(PostUniforms) == 64);

PostUniforms post_uniforms(const PostInputs& in);

// render::picture().present_scale: how the picture is scaled into the present rect.
enum class PresentScale : int { Integer = 0, Nearest = 1, Bilinear = 2, SharpBilinear = 3 };

// One scaled copy of the present. Source 0 = the frame, 1 = the sharp-bilinear intermediate; target likewise
// (0 = the swapchain texture).
struct BlitStep {
    bool from_intermediate = false;
    rhi::Rect src;
    bool to_intermediate = false;
    rhi::Rect dst;
    rhi::Filter filter = rhi::Filter::Nearest;
};

struct PresentPlan {
    int count = 0;
    std::array<BlitStep, 2> steps{};
    int intermediate_w = 0, intermediate_h = 0;  // > 0 when a step goes through the intermediate
};

// Frame of src_w x src_h into the window-space rect `dst` (top-left origin, like every RHI rect).
// native_render: a PSX-resolution target, whose pixels are not square, scales whole multiples on the lines.
PresentPlan plan_present(PresentScale mode, bool native_render, int src_w, int src_h, const rhi::Rect& dst);

class PostPass {
public:
    explicit PostPass(Renderer& owner);
    ~PostPass();
    PostPass(const PostPass&) = delete;
    PostPass& operator=(const PostPass&) = delete;

    // Post chain + blit of the scene target to the swapchain texture of `cmd`. Outside a render pass.
    void present(rhi::CommandList& cmd);
    // Runs after the picture reached the swapchain texture (w x h), still on `cmd`: what draws onto the window
    // itself (the overlay at native resolution).
    using AfterPresent = std::function<void(rhi::CommandList& cmd, rhi::TextureH swap, std::uint32_t w, std::uint32_t h)>;
    void set_after_present(AfterPresent fn) { after_present_ = std::move(fn); }

    // The presented frame (after post) at the scene size; alpha is written 255. Call between frames.
    bool frame_size(int* w, int* h) const;
    int read_frame_rgba(std::uint8_t* out, int max_bytes);

    // The settings of this frame from the live config (g_cfg_*).
    static PostInputs live_inputs();

    // GPU time of the HDR passes is in the profiler's "bloom", "hdr" and "hdr10" passes.
    HdrPass& hdr() { return *hdr_; }

private:
    bool ensure_pipeline();
    bool ensure_texture(rhi::TextureH& tex, int& tw, int& th, int w, int h, const char* name,
                        rhi::TextureFormat format = rhi::TextureFormat::RGBA8);
    void release();

    Renderer& owner_;
    rhi::ShaderH vs_, fs_;
    rhi::PipelineH pipeline_;
    rhi::SamplerH sampler_;
    rhi::BufferH triangle_;
    bool failed_ = false;
    rhi::TextureH post_;  // output of the post pass, at the scene size
    int post_w_ = 0, post_h_ = 0;
    rhi::TextureFormat post_format_ = rhi::TextureFormat::RGBA8;
    rhi::TextureH sharp_;  // sharp bilinear intermediate (a whole multiple of the scene size)
    int sharp_w_ = 0, sharp_h_ = 0;
    bool shown_post_ = false;  // the last present showed post_, not the scene colour
    rhi::TextureH shown_hdr_;  // or else the HDR resolve's output (scene size), when it ran
    std::unique_ptr<HdrPass> hdr_;
    std::uint32_t frame_ = 0;
    AfterPresent after_present_;
};

}  // namespace sh::gpu
