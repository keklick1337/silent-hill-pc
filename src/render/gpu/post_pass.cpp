// sh::gpu::PostPass (post_pass.h): the post pass of the preset from the scene target into a texture of its own,
// then the scaled copy of the shown frame into the swapchain texture inside the present rect over black.
#include "render/gpu/post_pass.h"

#include "engine/log/sh_log.h"
#include "render/gpu/hdr_pass.h"
#include "render/rhi/rhi_vulkan.h"
#include "render/gpu/renderer.h"
#include "render/gpu/shaders/post_shaders.h"
#include "render/gpu/render_state.h"
#include "render/gpu/targets.h"
#include "render/gpu/gpu_types.h"

#include <span>
#include <string_view>

namespace sh::gpu {

namespace {

using namespace sh::rhi;

constexpr std::uint32_t kSamplerScene = 0;
constexpr std::uint32_t kUniformPost = 0;
const std::string_view kFsSamplers[] = {"s_scene"};
const std::string_view kFsBlocks[] = {"PostFs"};
const std::string_view kPresentBlocks[] = {"PresentFs"};

// std140 PresentFs of shaders/present.frag.
struct PresentUniforms {
    float src[4];
    float dst[4];
    float win[4];  // xy: the window's size (a turned swapchain image, rhi_vulkan.cpp)
};

constexpr VertexAttribute kAttributes[] = {{0, VertexFormat::Float2, 0}};
// One triangle over the whole target; the part outside [-1, 1] is clipped.
constexpr float kTriangle[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};

}  // namespace

PostPass::PostPass(Renderer& owner) : owner_(owner), hdr_(std::make_unique<HdrPass>(owner)) {}

PostPass::~PostPass() { release(); }

void PostPass::release() {
    Device& dev = owner_.device();
    if (post_) dev.destroy(post_);
    if (sharp_) dev.destroy(sharp_);
    if (triangle_) dev.destroy(triangle_);
    if (sampler_) dev.destroy(sampler_);
    if (pipeline_) dev.destroy(pipeline_);
    if (fs_) dev.destroy(fs_);
    if (vs_) dev.destroy(vs_);
    if (present_pipeline_) dev.destroy(present_pipeline_);
    if (present_fs_) dev.destroy(present_fs_);
    if (present_vs_) dev.destroy(present_vs_);
    if (present_nearest_) dev.destroy(present_nearest_);
    if (present_linear_) dev.destroy(present_linear_);
    if (present_triangle_) dev.destroy(present_triangle_);
    present_pipeline_ = {};
    present_vs_ = present_fs_ = {};
    present_nearest_ = present_linear_ = {};
    present_triangle_ = {};
    present_uploaded_ = false;
    post_ = sharp_ = {};
    triangle_ = {};
    sampler_ = {};
    pipeline_ = {};
    vs_ = fs_ = {};
    post_w_ = post_h_ = sharp_w_ = sharp_h_ = 0;
}

PostInputs PostPass::live_inputs() {
    PostInputs in;
    post::PostSettings& s = in.settings;
    s.postProcess = sh::render::graphics().post_process;
    s.tonemap = sh::render::graphics().tonemap;
    s.fxaa = sh::render::graphics().fxaa != 0;
    s.brightness = sh::render::graphics().brightness;
    s.contrast = sh::render::graphics().contrast;
    s.saturation = sh::render::graphics().saturation;
    s.calibBar = sh::render::graphics().calibration_bar != 0;
    s.adjustActive =
        post::imageAdjustActive(sh::render::graphics().image_adjust_scope, sh::render::graphics().scene_frame != 0,
                                sh::render::graphics().calibration_bar != 0);
    in.post_intensity = sh::render::graphics().post_process_intensity;
    in.tonemap_intensity = sh::render::graphics().tonemap_intensity;
    return in;
}

bool PostPass::ensure_pipeline() {
    if (pipeline_) return true;
    if (failed_) return false;
    Device& dev = owner_.device();
    // A pass that cannot be built leaves the frame unprocessed rather than black; said once.
    auto fail = [this](std::string_view why) {
        failed_ = true;
        SH_WARN("[POST] post pass: %.*s; frames are shown without post-processing", int(why.size()), why.data());
        release();
        return false;
    };
    const auto vsrc = embedded::gpu_post_vert();
    const auto fsrc = embedded::gpu_post_frag();
    if (spirvBackend(dev.backend()) && (vsrc.spirv.empty() || fsrc.spirv.empty()))
        return fail("no SPIR-V in this build");

    ShaderDesc vd;
    vd.stage = ShaderStage::Vertex;
    vd.glsl = vsrc.glsl;
    vd.spirv = vsrc.spirv;
    vd.name = "post.vert";
    auto vs = dev.createShader(vd);
    if (!vs) return fail(vs.error());
    vs_ = vs.value();

    ShaderDesc fd;
    fd.stage = ShaderStage::Fragment;
    fd.glsl = fsrc.glsl;
    fd.spirv = fsrc.spirv;
    fd.samplerNames = kFsSamplers;
    fd.uniformBlockNames = kFsBlocks;
    fd.name = "post.frag";
    auto fs = dev.createShader(fd);
    if (!fs) return fail(fs.error());
    fs_ = fs.value();

    PipelineDesc p;
    p.vertexShader = vs_;
    p.fragmentShader = fs_;
    p.vertexLayout = {sizeof(float) * 2, kAttributes};
    p.primitive = PrimitiveType::TriangleList;
    p.colorFormat = TextureFormat::RGBA8;
    p.name = "post";
    auto pipe = dev.createPipeline(p);
    if (!pipe) return fail(pipe.error());
    pipeline_ = pipe.value();

    auto sampler = dev.createSampler({Filter::Linear, Filter::Linear, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    if (!sampler) return fail(sampler.error());
    sampler_ = sampler.value();

    auto tri = dev.createBuffer({BufferUsage::Vertex, sizeof(kTriangle), "post triangle"});
    if (!tri) return fail(tri.error());
    triangle_ = tri.value();
    return true;
}

bool PostPass::ensure_present_pipeline() {
    if (present_pipeline_) return true;
    if (present_failed_) return false;
    Device& dev = owner_.device();
    const auto vsrc = embedded::gpu_post_vert();
    const auto fsrc = embedded::gpu_present_frag();
    auto fail = [this]() {
        present_failed_ = true;
        return false;
    };
    if (vsrc.spirv.empty() || fsrc.spirv.empty()) return fail();
    ShaderDesc vd;
    vd.stage = ShaderStage::Vertex;
    vd.glsl = vsrc.glsl;
    vd.spirv = vsrc.spirv;
    vd.name = "post.vert (present)";
    auto vs = dev.createShader(vd);
    if (!vs) return fail();
    present_vs_ = vs.value();
    ShaderDesc fd;
    fd.stage = ShaderStage::Fragment;
    fd.glsl = fsrc.glsl;
    fd.spirv = fsrc.spirv;
    fd.samplerNames = kFsSamplers;
    fd.uniformBlockNames = kPresentBlocks;
    fd.name = "present.frag";
    auto fs = dev.createShader(fd);
    if (!fs) return fail();
    present_fs_ = fs.value();
    PipelineDesc p;
    p.vertexShader = present_vs_;
    p.fragmentShader = present_fs_;
    p.vertexLayout = {sizeof(float) * 2, kAttributes};
    p.primitive = PrimitiveType::TriangleList;
    p.colorFormat = TextureFormat::RGBA8;
    p.name = "present";
    auto pipe = dev.createPipeline(p);
    if (!pipe) return fail();
    present_pipeline_ = pipe.value();
    auto nearest =
        dev.createSampler({Filter::Nearest, Filter::Nearest, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    auto linear =
        dev.createSampler({Filter::Linear, Filter::Linear, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    auto tri = dev.createBuffer({BufferUsage::Vertex, sizeof(kTriangle), "present triangle"});
    if (!nearest || !linear || !tri) return fail();
    present_nearest_ = nearest.value();
    present_linear_ = linear.value();
    present_triangle_ = tri.value();
    return true;
}

bool PostPass::draw_present(CommandList& cmd, TextureH src, const BlitStep& step, TextureH out, bool covered,
                            std::uint32_t win_w, std::uint32_t win_h) {
    if (!ensure_present_pipeline()) return false;
    if (!present_uploaded_) {
        cmd.uploadBuffer(present_triangle_, 0, std::as_bytes(std::span(kTriangle)));
        present_uploaded_ = true;
    }
    RenderPassDesc rp;
    // Black bars where the picture does not cover the window; nothing to keep where it does.
    rp.color = {out, covered ? LoadOp::DontCare : LoadOp::Clear, StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
    rp.name = "present";
    RenderPass* pass = cmd.beginRenderPass(rp);
    if (!pass) return false;
    PresentUniforms u{{float(step.src.x), float(step.src.y), float(step.src.w), float(step.src.h)},
                      {float(step.dst.x), float(step.dst.y), float(step.dst.w), float(step.dst.h)},
                      {float(win_w), float(win_h), 0.f, 0.f}};
    pass->bindPipeline(present_pipeline_);
    pass->setScissor(step.dst);
    pass->bindVertexBuffer(present_triangle_, 0);
    pass->bindSampler(ShaderStage::Fragment, kSamplerScene, src,
                      step.filter == Filter::Linear ? present_linear_ : present_nearest_);
    pass->pushUniforms(ShaderStage::Fragment, kUniformPost, std::as_bytes(std::span(&u, 1)));
    pass->draw(3, 0);
    // The scene target is drawn into next frame: no sampler slot may keep it.
    pass->bindSampler(ShaderStage::Fragment, kSamplerScene, {}, {});
    cmd.endRenderPass();
    return true;
}

bool PostPass::ensure_texture(TextureH& tex, int& tw, int& th, int w, int h, const char* name, TextureFormat format) {
    if (tex && tw == w && th == h && (&tex != &post_ || post_format_ == format)) return true;
    Device& dev = owner_.device();
    if (tex) dev.destroy(tex);
    tex = {};
    tw = th = 0;
    auto t = dev.createTexture({format, std::uint32_t(w), std::uint32_t(h), kUsageSampled | kUsageColorTarget, name});
    if (!t) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[POST] %s %dx%d: %s", name, w, h, t.error().c_str());
        return false;
    }
    tex = t.value();
    tw = w;
    th = h;
    if (&tex == &post_) post_format_ = format;
    return true;
}

void PostPass::present(CommandList& cmd) {
    Device& dev = owner_.device();
    Targets& targets = owner_.targets();
    const int w = targets.width(), h = targets.height();
    shown_post_ = false;
    shown_hdr_ = {};

    // The HDR frame (hdr.h): bloom and the tonemapper first, so the post chain sees the display's range.
    const post::HdrSettings& hs = post::hdr();
    const bool hdr_frame = targets.scene_format() == TextureFormat::RGBA16F;
    rhi::vulkanSetHdrOutput(dev, {hs.output && hdr_frame, hs.paper_white, hs.peak});
    const bool hdr10 = hdr_frame && rhi::vulkanHdrStatus(dev).active;
    post::HdrResolvePlan hdr_plan;
    if (hdr_frame) hdr_plan = post::planResolve(hs, sh::render::graphics().scene_frame != 0, hdr10, w, h);
    // A phone's resolve draws the atmosphere the world left owed (Targets::world_layer) instead of a pass of its own.
    const Targets::WorldLayer* fold = nullptr;
    TextureH scene;
    if (hdr_frame && hdr_plan.run && phone_gpu() && targets.world_layer()) {
        scene = targets.resolved_unfinished(cmd);
        fold = targets.world_layer();
    } else {
        scene = targets.resolved(cmd);
    }
    if (!scene || w <= 0 || h <= 0) return;
    PostInputs in = live_inputs();
    // A phone draws the resolve straight into the window when nothing comes between it and the present (below).
    bool hdr_direct = false;
    if (hdr_frame) {
        hdr_direct = hdr_plan.run && !hdr10 && phone_gpu() && dev.backend() == Backend::Vulkan &&
                     !post::needsPostPass(in.settings);
        if (!hdr_direct) {
            const TextureH mapped = hdr_->resolve(cmd, scene, w, h, hdr_plan, hs.bloom_threshold, fold);
            if (mapped != scene) shown_hdr_ = scene = mapped;
        }
    }
    const TextureFormat post_format = hdr10 ? TextureFormat::RGBA16F : TextureFormat::RGBA8;

    if (!hdr_direct && post::needsPostPass(in.settings) && ensure_pipeline() &&
        ensure_texture(post_, post_w_, post_h_, w, h, "post", post_format)) {
        in.width = w;
        in.height = h;
        in.frame = ++frame_;
        in.sample_flip_y = spirvBackend(dev.backend());
        in.frag_coord_top_left = dev.caps().fragCoordTopLeft;
        PostUniforms u = post_uniforms(in);
        if (hdr10) u.texel[2] = hdr_plan.white;
        cmd.uploadBuffer(triangle_, 0, std::as_bytes(std::span(kTriangle)));
        RenderPassDesc rp;
        rp.color = {post_, LoadOp::DontCare, StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
        rp.name = "post";
        if (RenderPass* pass = cmd.beginRenderPass(rp)) {
            pass->bindPipeline(pipeline_);
            pass->setViewport({0.f, 0.f, float(w), float(h), 0.f, 1.f});
            pass->bindVertexBuffer(triangle_, 0);
            pass->bindSampler(ShaderStage::Fragment, kSamplerScene, scene, sampler_);
            pass->pushUniforms(ShaderStage::Fragment, kUniformPost, std::as_bytes(std::span(&u, 1)));
            pass->draw(3, 0);
            // The scene target is drawn into next frame: no sampler slot may keep it.
            pass->bindSampler(ShaderStage::Fragment, kSamplerScene, {}, {});
            cmd.endRenderPass();
            shown_post_ = true;
        }
    }
    std::uint32_t sw = 0, sh = 0;
    const TextureH swap = cmd.acquireSwapchainTexture(&sw, &sh);
    if (!swap) return;  // minimised or no window
    // An HDR10 swapchain (asked for above, or made just now) takes PQ values: the picture and the overlay are composed
    // in the frame's encoding first and encoded at the end.
    TextureH out = swap;
    const bool encode = rhi::vulkanHdrStatus(dev).active;
    if (encode) {
        if (const TextureH compose = hdr_->compose_target(int(sw), int(sh))) out = compose;
    }
    int dx = 0, dy = 0, dw = 0, dh = 0;
    targets.present_rect(&dx, &dy, &dw, &dh);
    if (dw <= 0 || dh <= 0) {
        dx = dy = 0;
        dw = int(sw);
        dh = int(sh);
    }
    const rhi::Rect dst{dx, dy, dw, dh};
    PresentPlan     plan = plan_present(PresentScale(sh::render::picture().present_scale),
                                        sh::render::picture().native_render != 0, w, h, dst);
    if (plan.intermediate_w > 0 && !ensure_texture(sharp_, sharp_w_, sharp_h_, plan.intermediate_w,
                                                   plan.intermediate_h, "present sharp")) {
        // Degrade to the smooth stretch, never to a black frame.
        plan = plan_present(PresentScale::Bilinear, false, w, h, dst);
    }
    // Black bars, and no stale edges where the picture does not cover the window. A picture over all of it needs no
    // clear: on a phone that is a pass of the screen's size.
    const BlitStep* last = plan.count > 0 ? &plan.steps[std::size_t(plan.count - 1)] : nullptr;
    const bool covered = last && !last->to_intermediate && last->dst.x == 0 && last->dst.y == 0 &&
                         last->dst.w == int(sw) && last->dst.h == int(sh);
    // A phone draws the last step in the render pass the overlay continues (the native backend merges the two):
    // the window is written once instead of being copied into, stored and loaded again.
    const bool draw_last = phone_gpu() && dev.backend() == Backend::Vulkan && last && !last->to_intermediate &&
                           ensure_present_pipeline();
    if (hdr_direct) {
        // One step at the scene's own size: the tonemapped frame goes into the window without a target of its own.
        // A scaled step keeps the resolve at the scene's size (fewer pixels to tonemap and spread the bloom over).
        const bool one_to_one = last && last->src.w == last->dst.w && last->src.h == last->dst.h;
        if (draw_last && plan.count == 1 && out == swap && one_to_one &&
            hdr_->present(cmd, scene, w, h, hdr_plan, hs.bloom_threshold, out, int(sw), int(sh), last->src, last->dst,
                          covered, last->filter == Filter::Linear ? present_linear_ : present_nearest_, fold)) {
            if (after_present_) after_present_(cmd, out, sw, sh);
            return;
        }
        const TextureH mapped = hdr_->resolve(cmd, scene, w, h, hdr_plan, hs.bloom_threshold, fold);
        if (mapped != scene) shown_hdr_ = scene = mapped;
    }
    const TextureH shown = shown_post_ ? post_ : scene;
    if (!covered && !draw_last) {
        RenderPassDesc clear;
        clear.color = {out, LoadOp::Clear, StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
        clear.name = "present";
        if (cmd.beginRenderPass(clear)) cmd.endRenderPass();
    }
    for (int i = 0; i < plan.count - (draw_last ? 1 : 0); ++i) {
        const BlitStep& s = plan.steps[std::size_t(i)];
        cmd.blitTexture(s.from_intermediate ? sharp_ : shown, s.src, s.to_intermediate ? sharp_ : out, s.dst, s.filter);
    }
    if (draw_last && !draw_present(cmd, last->from_intermediate ? sharp_ : shown, *last, out, covered, sw, sh)) {
        if (!covered) {
            RenderPassDesc clear;
            clear.color = {out, LoadOp::Clear, StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
            clear.name = "present";
            if (cmd.beginRenderPass(clear)) cmd.endRenderPass();
        }
        cmd.blitTexture(last->from_intermediate ? sharp_ : shown, last->src, out, last->dst, last->filter);
    }
    if (after_present_) after_present_(cmd, out, sw, sh);
    if (out != swap) hdr_->encode(cmd, out, swap, int(sw), int(sh), hs.paper_white, hs.peak);
}

bool PostPass::frame_size(int* w, int* h) const {
    // The size of what the last present showed: the post texture keeps it, the scene target may have resized.
    Targets& targets = owner_.targets();
    const int fw = shown_post_ ? post_w_ : targets.width();
    const int fh = shown_post_ ? post_h_ : targets.height();  // the HDR resolve's output has the scene size
    if (w) *w = fw;
    if (h) *h = fh;
    return fw > 0 && fh > 0;
}

int PostPass::read_frame_rgba(std::uint8_t* out, int max_bytes) {
    int w = 0, h = 0;
    if (!frame_size(&w, &h) || !out) return 0;
    const std::size_t bytes = std::size_t(w) * std::size_t(h) * 4;
    if (max_bytes < 0 || std::size_t(max_bytes) < bytes) return 0;
    const TextureH tex = shown_post_ ? post_ : shown_hdr_ ? shown_hdr_ : owner_.targets().scene_color();
    if (!tex) return 0;
    auto rb = owner_.device().readback(tex, {0, 0, w, h}, std::span(reinterpret_cast<std::byte*>(out), bytes));
    if (!rb) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[SHOT] frame readback %dx%d: %s", w, h, rb.error().c_str());
        return 0;
    }
    // The scene target's alpha is blend state, not coverage: a screenshot is opaque.
    for (std::size_t i = 3; i < bytes; i += 4) out[i] = 0xFF;
    return 1;
}

}  // namespace sh::gpu
