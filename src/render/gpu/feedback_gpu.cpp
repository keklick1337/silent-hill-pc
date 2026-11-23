// sh::gpu::FeedbackGpu (feedback_pass.h): the capture, pack and blank passes of the feedback store and the freeze
// frame on any rhi::Device. Kept apart from FeedbackPass so it links (and is tested) without a Renderer.

#include "render/gpu/feedback_pass.h"

#include "engine/log/sh_log.h"
#include "render/gpu/shaders/feedback_shaders.h"
#include "render/gpu/vram.h"

#include <span>
#include <string_view>

namespace sh::gpu {

namespace {

using namespace sh::rhi;

constexpr std::uint32_t kSamplerSrc = 0;
constexpr std::uint32_t kUniformFs = 0;
const std::string_view kFsSamplers[] = {"s_src"};
const std::string_view kCaptureBlocks[] = {"FeedbackCaptureFs"};
const std::string_view kPackBlocks[] = {"FeedbackPackFs"};

constexpr VertexAttribute kAttributes[] = {{0, VertexFormat::Float2, 0}};
constexpr float kTriangle[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};

struct alignas(16) CaptureUniforms {
    std::array<float, 4> src{};
    std::array<float, 4> dst{};
    std::array<float, 4> size{};
};
static_assert(sizeof(CaptureUniforms) == 48);

struct alignas(16) PackUniforms {
    std::array<float, 4> src{};
    std::array<float, 4> src_info{};
    std::array<float, 4> dst{};
    std::array<float, 4> params{};
};
static_assert(sizeof(PackUniforms) == 64);

bool inside_vram(const Rect& r) {
    return r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 && r.x + r.w <= Vram::kWidth && r.y + r.h <= Vram::kHeight;
}

std::size_t slot_index(CaptureSlot slot) { return slot == CaptureSlot::Pack ? 0 : 1; }

}  // namespace

FeedbackGpu::FeedbackGpu(Device& device) : device_(device) {}

FeedbackGpu::~FeedbackGpu() { release(); }

void FeedbackGpu::release() {
    for (Target* t : {&captures_[0], &captures_[1], &pack_target_, &freeze_target_}) {
        if (t->texture) device_.destroy(t->texture);
        *t = {};
    }
    if (triangle_) device_.destroy(triangle_);
    if (nearest_) device_.destroy(nearest_);
    if (linear_) device_.destroy(linear_);
    if (capture_pipe_) device_.destroy(capture_pipe_);
    if (pack_pipe_) device_.destroy(pack_pipe_);
    if (capture_fs_) device_.destroy(capture_fs_);
    if (pack_fs_) device_.destroy(pack_fs_);
    if (vs_) device_.destroy(vs_);
    triangle_ = {};
    nearest_ = linear_ = {};
    capture_pipe_ = pack_pipe_ = {};
    vs_ = capture_fs_ = pack_fs_ = {};
}

bool FeedbackGpu::pipelines_ready() {
    if (capture_pipe_ && pack_pipe_) return true;
    if (failed_) return false;
    // Without the passes the effects sample video memory as it is: degraded, never a crash. Said once.
    auto fail = [this](std::string_view why) {
        failed_ = true;
        SH_WARN("[FBFEEDBACK] feedback passes: %.*s; frame feedback effects are off", int(why.size()), why.data());
        for (Target* t : {&captures_[0], &captures_[1], &pack_target_}) {
            if (t->texture) device_.destroy(t->texture);
            *t = {};
        }
        return false;
    };
    const auto vsrc = embedded::gpu_feedback_quad_vert();
    const auto csrc = embedded::gpu_feedback_capture_frag();
    const auto psrc = embedded::gpu_feedback_pack_frag();
    if (spirvBackend(device_.backend()) && (vsrc.spirv.empty() || csrc.spirv.empty() || psrc.spirv.empty()))
        return fail("no SPIR-V in this build");

    auto shader = [this](ShaderStage stage, const embedded::Shader& src, std::span<const std::string_view> blocks,
                         const char* name) {
        ShaderDesc d;
        d.stage = stage;
        d.glsl = src.glsl;
        d.spirv = src.spirv;
        if (stage == ShaderStage::Fragment) {
            d.samplerNames = kFsSamplers;
            d.uniformBlockNames = blocks;
        }
        d.name = name;
        return device_.createShader(d);
    };
    auto vs = shader(ShaderStage::Vertex, vsrc, {}, "feedback_quad.vert");
    if (!vs) return fail(vs.error());
    vs_ = vs.value();
    auto cfs = shader(ShaderStage::Fragment, csrc, kCaptureBlocks, "feedback_capture.frag");
    if (!cfs) return fail(cfs.error());
    capture_fs_ = cfs.value();
    auto pfs = shader(ShaderStage::Fragment, psrc, kPackBlocks, "feedback_pack.frag");
    if (!pfs) return fail(pfs.error());
    pack_fs_ = pfs.value();

    auto pipeline = [this](ShaderH fs, TextureFormat format, const char* name) {
        PipelineDesc p;
        p.vertexShader = vs_;
        p.fragmentShader = fs;
        p.vertexLayout = {sizeof(float) * 2, kAttributes};
        p.primitive = PrimitiveType::TriangleList;
        p.colorFormat = format;
        p.name = name;
        return device_.createPipeline(p);
    };
    auto cp = pipeline(capture_fs_, TextureFormat::RGBA8, "feedback capture");
    if (!cp) return fail(cp.error());
    capture_pipe_ = cp.value();
    auto pp = pipeline(pack_fs_, TextureFormat::RG8, "feedback pack");
    if (!pp) return fail(pp.error());
    pack_pipe_ = pp.value();

    auto ns = device_.createSampler({Filter::Nearest, Filter::Nearest, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    if (!ns) return fail(ns.error());
    nearest_ = ns.value();
    auto ls = device_.createSampler({Filter::Linear, Filter::Linear, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    if (!ls) return fail(ls.error());
    linear_ = ls.value();

    auto tri = device_.createBuffer({BufferUsage::Vertex, sizeof(kTriangle), "feedback triangle"});
    if (!tri) return fail(tri.error());
    triangle_ = tri.value();
    return true;
}

bool FeedbackGpu::ensure_target(Target& t, int w, int h, TextureFormat format, const char* name) {
    if (w <= 0 || h <= 0) return false;
    if (t.texture && t.w == w && t.h == h && t.format == format) return true;
    if (t.texture) device_.destroy(t.texture);
    t = {};
    auto tex = device_.createTexture(
        {format, std::uint32_t(w), std::uint32_t(h), kUsageSampled | kUsageColorTarget, name});
    if (!tex) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[FBFEEDBACK] %s %dx%d: %s", name, w, h, tex.error().c_str());
        return false;
    }
    t.texture = tex.value();
    t.w = w;
    t.h = h;
    t.format = format;
    return true;
}

bool FeedbackGpu::capture(CommandList& cmd, TextureH scene, int scene_w, int scene_h, const CaptureStep& step) {
    if (!scene || scene_w <= 0 || scene_h <= 0 || !pipelines_ready()) return false;
    Target& t = captures_[slot_index(step.slot)];
    if (!ensure_target(t, step.w, step.h, TextureFormat::RGBA8,
                       step.slot == CaptureSlot::Pack ? "feedback capture" : "feedback scratch capture")) {
        stats_.failures++;
        return false;
    }
    CaptureUniforms u;
    if (step.blit.valid) {
        u.src = {step.blit.src.x0, step.blit.src.y0, step.blit.src.x1, step.blit.src.y1};
        u.dst = {float(step.blit.dx0), float(step.blit.dy0), float(step.blit.dx1), float(step.blit.dy1)};
    }
    // An invalid blit leaves u.dst empty: the capture is all black.
    u.size = {float(scene_w), float(scene_h), spirvBackend(device_.backend()) ? 1.f : 0.f, 0.f};

    cmd.uploadBuffer(triangle_, 0, std::as_bytes(std::span(kTriangle)));
    RenderPassDesc rp;
    rp.color = {t.texture, LoadOp::Clear, StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
    rp.name = "feedback";
    RenderPass* pass = cmd.beginRenderPass(rp);
    if (!pass) {
        stats_.failures++;
        return false;
    }
    pass->bindPipeline(capture_pipe_);
    pass->setViewport({0.f, 0.f, float(t.w), float(t.h), 0.f, 1.f});
    pass->bindVertexBuffer(triangle_, 0);
    pass->bindSampler(ShaderStage::Fragment, kSamplerSrc, scene, step.nearest ? nearest_ : linear_);
    pass->pushUniforms(ShaderStage::Fragment, kUniformFs, std::as_bytes(std::span(&u, 1)));
    pass->draw(3, 0);
    // The scene target is drawn into right after: no sampler slot may keep it.
    pass->bindSampler(ShaderStage::Fragment, kSamplerSrc, {}, {});
    cmd.endRenderPass();
    stats_.captures++;
    return true;
}

bool FeedbackGpu::pack(CommandList& cmd, TextureH src, int src_w, int src_h, const rhi::Rect& src_rect,
                       bool bottom_up, TextureH vram, const Rect& dst, const PackParams& params) {
    if (!src || !vram || src_w <= 0 || src_h <= 0 || src_rect.w <= 0 || src_rect.h <= 0) return false;
    if (!inside_vram(dst)) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[FBFEEDBACK] pack rect (%d,%d %dx%d) is not inside video memory", dst.x, dst.y, dst.w, dst.h);
        return false;
    }
    if (!pipelines_ready()) return false;
    if (!ensure_target(pack_target_, Vram::kWidth, Vram::kHeight, TextureFormat::RG8, "feedback pack")) {
        stats_.failures++;
        return false;
    }
    PackUniforms u;
    u.src = {float(src_rect.x), float(src_rect.y), float(src_rect.w), float(src_rect.h)};
    u.src_info = {float(src_w), float(src_h), bottom_up ? 1.f : 0.f, 0.f};
    u.dst = {float(dst.w), float(dst.h), 0.f, 0.f};
    u.params = {params.gain, params.mask ? 1.f : 0.f, float(static_cast<int>(params.mode)), params.decay ? 1.f : 0.f};

    cmd.uploadBuffer(triangle_, 0, std::as_bytes(std::span(kTriangle)));
    RenderPassDesc rp;
    rp.color = {pack_target_.texture, LoadOp::DontCare, StoreOp::Store, {0.f, 0.f, 0.f, 0.f}};
    rp.name = "feedback";
    RenderPass* pass = cmd.beginRenderPass(rp);
    if (!pass) {
        stats_.failures++;
        return false;
    }
    // The words must land in storage rows 0..h-1, the rows the copy reads: on GL the viewport's top-left origin
    // is flipped, so the rect sits at the bottom of the target in RHI terms.
    const float vy = device_.backend() == Backend::Gl33 ? float(pack_target_.h - dst.h) : 0.f;
    pass->bindPipeline(pack_pipe_);
    pass->setViewport({0.f, vy, float(dst.w), float(dst.h), 0.f, 1.f});
    pass->bindVertexBuffer(triangle_, 0);
    pass->bindSampler(ShaderStage::Fragment, kSamplerSrc, src, nearest_);
    pass->pushUniforms(ShaderStage::Fragment, kUniformFs, std::as_bytes(std::span(&u, 1)));
    pass->draw(3, 0);
    pass->bindSampler(ShaderStage::Fragment, kSamplerSrc, {}, {});
    cmd.endRenderPass();
    // A storage-to-storage copy on both backends, as Vram::upload writes rows.
    cmd.copyTexture(pack_target_.texture, {0, 0, dst.w, dst.h}, vram, dst.x, dst.y);
    stats_.packs++;
    return true;
}

bool FeedbackGpu::pack_from(CommandList& cmd, CaptureSlot slot, TextureH vram, const Rect& dst,
                            const PackParams& params) {
    const Target& t = captures_[slot_index(slot)];
    if (!t.texture) return false;
    return pack(cmd, t.texture, t.w, t.h, {0, 0, t.w, t.h}, true, vram, dst, params);
}

void FeedbackGpu::clear(CommandList& cmd, TextureH vram, const Rect& dst) {
    if (!vram || !inside_vram(dst)) return;
    zeros_.assign(std::size_t(dst.w) * std::size_t(dst.h) * 2, std::byte{0});
    cmd.uploadTexture(vram, {dst.x, dst.y, dst.w, dst.h}, zeros_);
    stats_.clears++;
}

TextureH FeedbackGpu::capture_texture(CaptureSlot slot) const { return captures_[slot_index(slot)].texture; }

void FeedbackGpu::capture_size(CaptureSlot slot, int* w, int* h) const {
    const Target& t = captures_[slot_index(slot)];
    if (w) *w = t.w;
    if (h) *h = t.h;
}

void FeedbackGpu::capture_last_frame(CommandList& cmd, TextureH scene, TextureFormat format, int w, int h, bool hold) {
    // A frame that re-presented the capture is not captured again, or the UI drawn on it would bake in.
    if (!freeze_.shouldCapture(hold)) return;
    if (!scene || w <= 0 || h <= 0) return;
    if (freeze_.sizeDiffers(w, h)) freeze_.resized(w, h);
    // The scene's own format, so an HDR frame keeps its highlights while the menu shows it again.
    if (!ensure_target(freeze_target_, w, h, format, "freeze frame")) return;
    cmd.blitTexture(scene, {0, 0, w, h}, freeze_target_.texture, {0, 0, w, h}, Filter::Nearest);
    freeze_.captured();
}

bool FeedbackGpu::present_last_frame(CommandList& cmd, TextureH scene, int w, int h) {
    if (!freeze_.valid() || !freeze_target_.texture || !scene || w <= 0 || h <= 0) return false;
    cmd.blitTexture(freeze_target_.texture, {0, 0, freeze_target_.w, freeze_target_.h}, scene, {0, 0, w, h},
                    Filter::Nearest);
    freeze_.presented();
    return true;
}

}  // namespace sh::gpu
