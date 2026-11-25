// sh::gpu::HdrPass (hdr_pass.h): the bloom chain, the resolve of the HDR frame and the HDR10 encode.
#include "render/gpu/hdr_pass.h"

#include "engine/log/sh_log.h"
#include "render/gpu/gpu_types.h"
#include "render/gpu/renderer.h"
#include "render/gpu/shaders/hdr_shaders.h"
#include "render/gpu/shaders/post_shaders.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace sh::gpu {

namespace {

using namespace sh::rhi;

const std::string_view kSrcSamplers[] = {"s_src"};
const std::string_view kBloomUpSamplers[] = {"s_src", "s_base"};
const std::string_view kResolveSamplers[] = {"s_scene", "s_bloom",  "s_bloom2", "s_layer",
                                             "s_depth", "s_layerZ", "s_result"};
const std::string_view kBloomBlocks[] = {"BloomFs"};
const std::string_view kBloomUpBlocks[] = {"BloomUpFs"};
const std::string_view kResolveBlocks[] = {"ResolveFs"};
const std::string_view kEncodeBlocks[] = {"EncodeFs"};

constexpr VertexAttribute kAttributes[] = {{0, VertexFormat::Float2, 0}};
constexpr float kTriangle[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};

struct alignas(16) BloomUniforms {
    std::array<float, 4> p{};      // x: mode, y: threshold, z: flip v
    std::array<float, 4> texel{};  // xy: 1 / source size
};
static_assert(sizeof(BloomUniforms) == 32);

struct alignas(16) BloomUpUniforms {
    std::array<float, 4> p{};      // x: weight of the level below, z: flip v
    std::array<float, 4> texel{};  // xy: 1 / size of the level below
};
static_assert(sizeof(BloomUpUniforms) == 32);

struct alignas(16) EncodeUniforms {
    std::array<float, 4> p{};  // x: paper white nits, y: peak nits, z: flip v
};
static_assert(sizeof(EncodeUniforms) == 16);

// kPrefilterBox: a phone's prefilter, one bilinear tap (the 2x2 box of the scene) with the threshold after the mean.
enum BloomMode { kPrefilter = 0, kDown = 1, kUp = 2, kPrefilterBox = 3 };

}  // namespace

// The phone chain needs the level below the first to be the upsampled rest or the last level itself.
static_assert(post::kMobileBloomLevels >= 2);

HdrPass::HdrPass(Renderer& owner) : owner_(owner), mobile_(phone_gpu()) {}

HdrPass::~HdrPass() { release(); }

void HdrPass::release() {
    Device& dev = owner_.device();
    for (auto* chain : {&bloom_, &bloom_up_}) {
        for (Target& t : *chain) {
            if (t.texture) dev.destroy(t.texture);
            t = {};
        }
    }
    for (Target* t : {&out_, &compose_}) {
        if (t->texture) dev.destroy(t->texture);
        *t = {};
    }
    for (PipelineH* p : {&bloom_pipe_, &bloom_add_pipe_, &bloom_up_pipe_, &resolve_pipe_, &encode_pipe_}) {
        if (*p) dev.destroy(*p);
        *p = {};
    }
    for (ShaderH* s : {&vs_, &bloom_fs_, &bloom_up_fs_, &resolve_fs_, &encode_fs_}) {
        if (*s) dev.destroy(*s);
        *s = {};
    }
    if (linear_) dev.destroy(linear_);
    if (nearest_) dev.destroy(nearest_);
    nearest_ = {};
    if (triangle_) dev.destroy(triangle_);
    linear_ = {};
    triangle_ = {};
}

bool HdrPass::ensure_pipelines() {
    if (encode_pipe_) return true;
    if (failed_) return false;
    Device& dev = owner_.device();
    // Said once: the frame then goes on as the scene target holds it, clipped by the post pass or the present.
    auto fail = [this](std::string_view why) {
        failed_ = true;
        SH_WARN("[HDR] passes: %.*s; the HDR frame is shown without bloom and tonemapping", int(why.size()),
                why.data());
        release();
        return false;
    };
    auto shader = [&](ShaderStage stage, const embedded::Shader& src, std::span<const std::string_view> samplers,
                      std::span<const std::string_view> blocks, std::string_view name, ShaderH* out) {
        if (src.spirv.empty()) return false;
        ShaderDesc d;
        d.stage = stage;
        d.glsl = src.glsl;
        d.spirv = src.spirv;
        d.samplerNames = samplers;
        d.uniformBlockNames = blocks;
        d.name = name;
        auto s = dev.createShader(d);
        if (!s) return false;
        *out = s.value();
        return true;
    };
    if (!shader(ShaderStage::Vertex, embedded::gpu_post_vert(), {}, {}, "post.vert", &vs_) ||
        !shader(ShaderStage::Fragment, embedded::gpu_hdr_bloom_frag(), kSrcSamplers, kBloomBlocks, "hdr_bloom.frag",
                &bloom_fs_) ||
        (mobile_ && !shader(ShaderStage::Fragment, embedded::gpu_hdr_bloom_up_frag(), kBloomUpSamplers, kBloomUpBlocks,
                            "hdr_bloom_up.frag", &bloom_up_fs_)) ||
        !shader(ShaderStage::Fragment, embedded::gpu_hdr_resolve_frag(), kResolveSamplers, kResolveBlocks,
                "hdr_resolve.frag", &resolve_fs_) ||
        !shader(ShaderStage::Fragment, embedded::gpu_hdr10_encode_frag(), kSrcSamplers, kEncodeBlocks,
                "hdr10_encode.frag", &encode_fs_))
        return fail("a shader did not build");

    auto pipeline = [&](ShaderH fs, TextureFormat format, bool additive, std::string_view name, PipelineH* out) {
        PipelineDesc p;
        p.vertexShader = vs_;
        p.fragmentShader = fs;
        p.vertexLayout = {sizeof(float) * 2, kAttributes};
        p.primitive = PrimitiveType::TriangleList;
        p.colorFormat = format;
        if (additive) {
            p.blend.enable = true;
            p.blend.srcColor = p.blend.dstColor = BlendFactor::One;
            p.blend.srcAlpha = BlendFactor::Zero;
            p.blend.dstAlpha = BlendFactor::One;
        }
        p.name = name;
        auto made = dev.createPipeline(p);
        if (!made) return false;
        *out = made.value();
        return true;
    };
    if (!pipeline(bloom_fs_, TextureFormat::RGBA16F, false, "hdr bloom", &bloom_pipe_) ||
        !pipeline(bloom_fs_, TextureFormat::RGBA16F, true, "hdr bloom add", &bloom_add_pipe_) ||
        (mobile_ && !pipeline(bloom_up_fs_, TextureFormat::RGBA16F, false, "hdr bloom up", &bloom_up_pipe_)) ||
        !pipeline(resolve_fs_, TextureFormat::RGBA8, false, "hdr resolve", &resolve_pipe_) ||
        !pipeline(encode_fs_, TextureFormat::RGBA8, false, "hdr10 encode", &encode_pipe_))
        return fail("a pipeline did not build");

    auto sampler =
        dev.createSampler({Filter::Linear, Filter::Linear, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    if (!sampler) return fail(sampler.error());
    linear_ = sampler.value();
    auto nearest =
        dev.createSampler({Filter::Nearest, Filter::Nearest, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    if (!nearest) return fail(nearest.error());
    nearest_ = nearest.value();
    auto tri = dev.createBuffer({BufferUsage::Vertex, sizeof(kTriangle), "hdr triangle"});
    if (!tri) return fail(tri.error());
    triangle_ = tri.value();
    return true;
}

bool HdrPass::ensure(Target& t, int w, int h, TextureFormat format, const char* name) {
    if (t.texture && t.w == w && t.h == h && t.format == format) return true;
    Device& dev = owner_.device();
    if (t.texture) dev.destroy(t.texture);
    t = {};
    auto tex = dev.createTexture({format, std::uint32_t(w), std::uint32_t(h), kUsageSampled | kUsageColorTarget, name});
    if (!tex) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[HDR] %s %dx%d: %s", name, w, h, tex.error().c_str());
        return false;
    }
    t = {tex.value(), w, h, format};
    return true;
}

void HdrPass::fullscreen(RenderPass& pass, PipelineH pipe, int w, int h) {
    pass.bindPipeline(pipe);
    pass.setViewport({0.f, 0.f, float(w), float(h), 0.f, 1.f});
    pass.bindVertexBuffer(triangle_, 0);
}

HdrPass::Bloomed HdrPass::bloom(CommandList& cmd, TextureH scene, int w, int h, const post::HdrResolvePlan& plan,
                                float threshold) {
    const float flip = spirvBackend(owner_.device().backend()) ? 1.f : 0.f;
    cmd.uploadBuffer(triangle_, 0, std::as_bytes(std::span(kTriangle)));

    // The desktop chain sums every level; `levels` stays its count in the resolve's 1 / levels on phones as well,
    // where only `chain` levels are drawn (post::mobileBloomChain).
    int levels = plan.bloom > 0.f ? plan.bloomLevels : 0;
    const post::BloomChain mc = post::mobileBloomChain(mobile_ ? levels : 0);
    const int chain = mobile_ ? mc.levels : levels;
    for (int i = 0; i < chain; ++i) {
        const int lw = std::max(1, w >> (i + 1)), lh = std::max(1, h >> (i + 1));
        const bool up = mobile_ && i >= 1 && i <= chain - 2;
        if (!ensure(bloom_[std::size_t(i)], lw, lh, TextureFormat::RGBA16F, "hdr bloom") ||
            (up && !ensure(bloom_up_[std::size_t(i)], lw, lh, TextureFormat::RGBA16F, "hdr bloom up"))) {
            levels = 0;  // the frame without bloom rather than a broken one
            break;
        }
    }
    auto bloom_pass = [&](int dst, TextureH src, int src_w, int src_h, int mode) {
        const Target& t = bloom_[std::size_t(dst)];
        RenderPassDesc rp;
        rp.color = {t.texture, mode == kUp ? LoadOp::Load : LoadOp::DontCare, StoreOp::Store, {0.f, 0.f, 0.f, 0.f}};
        rp.name = mode == kPrefilter || mode == kPrefilterBox ? "bloom prefilter" : "bloom";
        RenderPass* pass = cmd.beginRenderPass(rp);
        if (!pass) return;
        fullscreen(*pass, mode == kUp ? bloom_add_pipe_ : bloom_pipe_, t.w, t.h);
        BloomUniforms u;
        u.p = {float(mode), threshold, flip, 0.f};
        u.texel = {1.f / float(src_w), 1.f / float(src_h), 0.f, 0.f};
        pass->bindSampler(ShaderStage::Fragment, 0, src, linear_);
        pass->pushUniforms(ShaderStage::Fragment, 0, std::as_bytes(std::span(&u, 1)));
        pass->draw(3, 0);
        pass->bindSampler(ShaderStage::Fragment, 0, {}, {});
        cmd.endRenderPass();
    };
    // Phones: bloom_up_[dst] = bloom_[dst] + weight * tent(src), into a target that is never loaded.
    auto bloom_up_pass = [&](int dst, const Target& src, float weight) {
        const Target& t = bloom_up_[std::size_t(dst)];
        RenderPassDesc rp;
        rp.color = {t.texture, LoadOp::DontCare, StoreOp::Store, {0.f, 0.f, 0.f, 0.f}};
        rp.name = "bloom up";
        RenderPass* pass = cmd.beginRenderPass(rp);
        if (!pass) return;
        fullscreen(*pass, bloom_up_pipe_, t.w, t.h);
        BloomUpUniforms u;
        u.p = {weight, 0.f, flip, 0.f};
        u.texel = {1.f / float(src.w), 1.f / float(src.h), 0.f, 0.f};
        pass->bindSampler(ShaderStage::Fragment, 0, src.texture, linear_);
        pass->bindSampler(ShaderStage::Fragment, 1, bloom_[std::size_t(dst)].texture, linear_);
        pass->pushUniforms(ShaderStage::Fragment, 0, std::as_bytes(std::span(&u, 1)));
        pass->draw(3, 0);
        pass->bindSampler(ShaderStage::Fragment, 0, {}, {});
        pass->bindSampler(ShaderStage::Fragment, 1, {}, {});
        cmd.endRenderPass();
    };
    // The texture the phone resolve spreads next to the first level, and its weight (0: none).
    const Target* second = nullptr;
    float second_weight = 0.f;
    if (levels > 0) {
        bloom_pass(0, scene, w, h, mobile_ ? kPrefilterBox : kPrefilter);
        for (int i = 1; i < chain; ++i) {
            const Target& src = bloom_[std::size_t(i - 1)];
            bloom_pass(i, src.texture, src.w, src.h, kDown);
        }
        if (!mobile_) {
            for (int i = levels - 1; i > 0; --i) {
                const Target& src = bloom_[std::size_t(i)];
                bloom_pass(i - 1, src.texture, src.w, src.h, kUp);
            }
        } else if (chain >= 2) {
            // The last level is read where it is; the first is upsampled onto in the resolve.
            second = &bloom_[std::size_t(chain - 1)];
            second_weight = mc.lastWeight;
            for (int i = chain - 2; i >= 1; --i) {
                bloom_up_pass(i, *second, second_weight);
                second = &bloom_up_[std::size_t(i)];
                second_weight = 1.f;
            }
        }
    }

    Bloomed b;
    b.u.mode = {float(int(plan.tonemap)), levels > 0 ? plan.bloom : 0.f, threshold, flip};
    b.u.out = {plan.white, levels > 0 ? 1.f / float(levels) : 0.f, mobile_ ? 1.f : 0.f, second ? second_weight : 0.f};
    if (levels > 0) b.u.texel = {0.f, 0.f, 1.f / float(bloom_[0].w), 1.f / float(bloom_[0].h)};
    if (second) {
        b.u.texel[0] = 1.f / float(second->w);
        b.u.texel[1] = 1.f / float(second->h);
    }
    b.bloom = levels > 0 ? bloom_[0].texture : scene;
    b.bloom2 = second ? second->texture : scene;
    return b;
}

void HdrPass::draw_resolve(RenderPass& pass, TextureH scene, Bloomed& b, SamplerH scene_sampler,
                           const Targets::WorldLayer* fold) {
    if (fold && fold->layer) {
        b.u.layer = {1.f, float(fold->step), fold->sz_max, 0.f};
        b.u.world = {fold->view.x, fold->view.y, fold->view.w, fold->view.h};
    }
    if (fold && fold->result) b.u.result = {1.f, fold->sharpness, 0.f, 0.f};
    pass.pushUniforms(ShaderStage::Fragment, 0, std::as_bytes(std::span(&b.u, 1)));
    pass.bindSampler(ShaderStage::Fragment, 0, scene, scene_sampler ? scene_sampler : linear_);
    pass.bindSampler(ShaderStage::Fragment, 1, b.bloom, linear_);
    pass.bindSampler(ShaderStage::Fragment, 2, b.bloom2, linear_);
    if (fold) {
        // Unbound slots read the backend's dummy texture; only the parts the uniforms name are read.
        pass.bindSampler(ShaderStage::Fragment, 3, fold->layer, linear_);
        pass.bindSampler(ShaderStage::Fragment, 4, fold->depth, nearest_);
        pass.bindSampler(ShaderStage::Fragment, 5, fold->layer_z, nearest_);
        pass.bindSampler(ShaderStage::Fragment, 6, fold->result, nearest_);
    }
    pass.draw(3, 0);
    // The scene target is drawn into next frame: no sampler slot may keep it.
    for (std::uint32_t slot = 0; slot < (fold ? 7u : 3u); ++slot) pass.bindSampler(ShaderStage::Fragment, slot, {}, {});
}

TextureH HdrPass::resolve(CommandList& cmd, TextureH scene, int w, int h, const post::HdrResolvePlan& plan,
                          float threshold, const Targets::WorldLayer* fold) {
    if (!plan.run || !scene || w <= 0 || h <= 0 || !ensure_pipelines()) return scene;
    // The bloom spreads the picture the resolve shows: the temporal result where it stands for the scene.
    Bloomed b = bloom(cmd, fold && fold->result ? fold->result : scene, w, h, plan, threshold);
    const TextureFormat format = plan.white > 1.f ? TextureFormat::RGBA16F : TextureFormat::RGBA8;
    if (!ensure(out_, w, h, format, "hdr resolve")) return scene;
    RenderPassDesc rp;
    rp.color = {out_.texture, LoadOp::DontCare, StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
    rp.name = "hdr";
    RenderPass* pass = cmd.beginRenderPass(rp);
    if (!pass) return scene;
    fullscreen(*pass, resolve_pipe_, w, h);
    draw_resolve(*pass, scene, b, {}, fold);
    cmd.endRenderPass();
    return out_.texture;
}

bool HdrPass::present(CommandList& cmd, TextureH scene, int w, int h, const post::HdrResolvePlan& plan, float threshold,
                      TextureH window, int win_w, int win_h, const rhi::Rect& src, const rhi::Rect& dst, bool covered,
                      SamplerH scene_sampler, const Targets::WorldLayer* fold) {
    if (!plan.run || !scene || w <= 0 || h <= 0 || !ensure_pipelines()) return false;
    Bloomed b = bloom(cmd, fold && fold->result ? fold->result : scene, w, h, plan, threshold);
    b.u.src = {float(src.x), float(src.y), float(src.w), float(src.h)};
    b.u.dst = {float(dst.x), float(dst.y), float(dst.w), float(dst.h)};
    b.u.win = {float(win_w), float(win_h), 0.f, 0.f};
    RenderPassDesc rp;
    // As PostPass::draw_present: black bars where the picture does not cover the window.
    rp.color = {window, covered ? LoadOp::DontCare : LoadOp::Clear, StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
    rp.name = "present";
    RenderPass* pass = cmd.beginRenderPass(rp);
    if (!pass) return false;
    pass->bindPipeline(resolve_pipe_);
    pass->setScissor(dst);
    pass->bindVertexBuffer(triangle_, 0);
    draw_resolve(*pass, scene, b, scene_sampler, fold);
    cmd.endRenderPass();
    return true;
}

TextureH HdrPass::compose_target(int w, int h) {
    if (w <= 0 || h <= 0 || !ensure_pipelines()) return {};
    return ensure(compose_, w, h, TextureFormat::RGBA16F, "hdr10 compose") ? compose_.texture : TextureH{};
}

void HdrPass::encode(CommandList& cmd, TextureH compose, TextureH swap, int w, int h, float paperWhiteNits,
                     float peakNits) {
    if (!compose || !swap || !ensure_pipelines()) return;
    cmd.uploadBuffer(triangle_, 0, std::as_bytes(std::span(kTriangle)));
    RenderPassDesc rp;
    rp.color = {swap, LoadOp::DontCare, StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
    rp.name = "hdr10";
    RenderPass* pass = cmd.beginRenderPass(rp);
    if (!pass) return;
    fullscreen(*pass, encode_pipe_, w, h);
    EncodeUniforms u;
    u.p = {paperWhiteNits, peakNits, spirvBackend(owner_.device().backend()) ? 1.f : 0.f, 0.f};
    pass->bindSampler(ShaderStage::Fragment, 0, compose, linear_);
    pass->pushUniforms(ShaderStage::Fragment, 0, std::as_bytes(std::span(&u, 1)));
    pass->draw(3, 0);
    pass->bindSampler(ShaderStage::Fragment, 0, {}, {});
    cmd.endRenderPass();
}

}  // namespace sh::gpu
