// sh::gpu split drawing (render/gpu/split_renderer.h): the pipeline key and uniform blocks of a split, the item-pass
// order, and what one split records on a device (a fake one that keeps the calls).

#include "framework/sh_test.h"
#include "render/gpu/split_renderer.h"
#include "render/rhi/prim_batch.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace sh;
using gpu::Blend;
using gpu::Split;

class FakePass : public rhi::RenderPass {
public:
    struct Sampler {
        std::uint32_t slot, texture;
    };
    std::vector<std::uint32_t> pipelines;
    std::vector<Sampler> samplers;
    std::vector<std::pair<int, std::uint32_t>> pushes;  // (stage * 10 + slot, size)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> draws;  // (first, count)
    std::array<float, 4> blend{};
    rhi::PrimFsUniforms last_fs{};

    void bindPipeline(rhi::PipelineH p) override { pipelines.push_back(p.raw); }
    void setViewport(const rhi::Viewport&) override {}
    void setScissor(const rhi::Rect&) override {}
    void setBlendConstants(const std::array<float, 4>& rgba) override { blend = rgba; }
    void setStencilReference(std::uint8_t) override {}
    void bindVertexBuffer(rhi::BufferH, std::uint32_t) override {}
    void bindIndexBuffer(rhi::BufferH, rhi::IndexType, std::uint32_t) override {}
    void bindSampler(rhi::ShaderStage, std::uint32_t slot, rhi::TextureH t, rhi::SamplerH) override {
        samplers.push_back({slot, t.raw});
    }
    void pushUniforms(rhi::ShaderStage stage, std::uint32_t slot, std::span<const std::byte> data) override {
        pushes.push_back({static_cast<int>(stage) * 10 + static_cast<int>(slot), static_cast<std::uint32_t>(data.size())});
        if (stage == rhi::ShaderStage::Fragment && slot == 0 && data.size() == sizeof last_fs)
            std::memcpy(&last_fs, data.data(), sizeof last_fs);
    }
    void draw(std::uint32_t count, std::uint32_t first) override { draws.push_back({first, count}); }
    void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}
};

class FakeDevice : public rhi::Device {
public:
    std::vector<rhi::PipelineDesc> pipelines;
    std::uint32_t next = 1;
    rhi::Caps caps_;

    rhi::Backend backend() const override { return rhi::Backend::Gl33; }
    const rhi::Caps& caps() const override { return caps_; }
    Result<rhi::TextureH> createTexture(const rhi::TextureDesc&) override { return Result<rhi::TextureH>::ok({next++}); }
    Result<rhi::BufferH> createBuffer(const rhi::BufferDesc&) override { return Result<rhi::BufferH>::ok({next++}); }
    Result<rhi::SamplerH> createSampler(const rhi::SamplerDesc&) override { return Result<rhi::SamplerH>::ok({next++}); }
    Result<rhi::ShaderH> createShader(const rhi::ShaderDesc&) override { return Result<rhi::ShaderH>::ok({next++}); }
    Result<rhi::PipelineH> createPipeline(const rhi::PipelineDesc& d) override {
        pipelines.push_back(d);
        return Result<rhi::PipelineH>::ok({next++});
    }
    void destroy(rhi::TextureH) override {}
    void destroy(rhi::BufferH) override {}
    void destroy(rhi::SamplerH) override {}
    void destroy(rhi::ShaderH) override {}
    void destroy(rhi::PipelineH) override {}
    rhi::CommandList* begin() override { return nullptr; }
    Result<void> submit() override { return Result<void>::ok(); }
    Result<void> readback(rhi::TextureH, const rhi::Rect&, std::span<std::byte>) override {
        return Result<void>::fail("fake");
    }
    void waitIdle() override {}
};

Split prims(Blend blend, std::uint32_t first, std::uint32_t count) {
    Split s;
    s.kind = Split::Kind::Prims;
    s.blend = blend;
    s.first = first;
    s.count = count;
    return s;
}

std::vector<gpu::Vertex> triangles(int n, float z0 = 0.f, float dz = 1.f) {
    std::vector<gpu::Vertex> v(static_cast<std::size_t>(n) * 3);
    for (int t = 0; t < n; ++t)
        for (int k = 0; k < 3; ++k) v[static_cast<std::size_t>(t) * 3 + k].z = z0 + dz * static_cast<float>(t);
    return v;
}

}  // namespace

SH_TEST(gpu_split_key_opaque_and_blended) {
    gpu::ShadeInputs in;
    gpu::SplitContext ctx;
    const auto v = triangles(1);

    gpu::SplitDraw d = gpu::plan_split(prims(Blend::None, 0, 3), v, in, ctx);
    SH_CHECK(!d.skip);
    SH_CHECK(d.key.blend == Blend::None);
    SH_CHECK(d.key.depthTest);  // opaque: depth on with the PGXP depth buffer
    SH_CHECK(d.key.depthWrite);
    SH_CHECK(!d.key.depthAlways);
    SH_CHECK(d.key.stencil == rhi::StencilMode::MaskTest);

    Split masked = prims(Blend::Average, 0, 3);
    masked.mask_mode = 1;
    d = gpu::plan_split(masked, v, in, ctx);
    SH_CHECK(!d.key.depthTest);  // blended: depth off
    SH_CHECK(d.key.stencil == rhi::StencilMode::MaskWrite);
    SH_CHECK(d.fs.modes[0] == 0.f);

    d = gpu::plan_split(prims(Blend::Subtract, 0, 3), v, in, ctx);
    SH_CHECK(d.fs.modes[0] == 1.f);  // fog to black

    in.pgxp_zbuffer = false;
    d = gpu::plan_split(prims(Blend::None, 0, 3), v, in, ctx);
    SH_CHECK(!d.key.depthTest);
    ctx.force_item_depth = true;
    d = gpu::plan_split(prims(Blend::Average, 0, 3), v, in, ctx);
    SH_CHECK(d.key.depthTest);  // the item bracket keeps depth on for translucent faces

    SH_CHECK(gpu::plan_split(prims(Blend::None, 0, 0), v, in, ctx).skip);
}

SH_TEST(gpu_split_world_depth_and_scene_tested) {
    gpu::ShadeInputs in;
    in.pgxp = true;
    gpu::SplitContext ctx;
    auto v = triangles(1);

    // The engine's scene owns the world's depth: world prims are never drawn in painter order.
    Split world = prims(Blend::None, 0, 3);
    world.depth = gpu::DepthMode::World;
    SH_CHECK(!gpu::plan_split(world, v, in, ctx).key.depthAlways);

    // A translucent 3D prim tests the scene's depth without writing it; a 2D one (no view depth) does not.
    Split glow = prims(Blend::Add, 0, 3);
    gpu::SplitDraw d = gpu::plan_split(glow, v, in, ctx);
    SH_CHECK(!d.key.depthTest);
    v[0].ppw = 1.f;
    d = gpu::plan_split(glow, v, in, ctx);
    SH_CHECK(d.key.depthTest);
    SH_CHECK(!d.key.depthWrite);

    // Item pass: translucent faces do not write depth, opaque ones do.
    ctx.item_pass = true;
    SH_CHECK(!gpu::plan_split(prims(Blend::Average, 0, 3), v, in, ctx).key.depthWrite);
    SH_CHECK(gpu::plan_split(prims(Blend::None, 0, 3), v, in, ctx).key.depthWrite);
}

SH_TEST(gpu_split_uniforms) {
    gpu::ShadeInputs in;
    gpu::SplitContext ctx;
    ctx.projection = rhi::orthoProjection(0, 320, 240, 0, 0, 1);
    ctx.target_height = 480.f;
    const auto v = triangles(1);
    in.pgxp = true;
    in.sz_max = 1234.f;
    in.soft_fog = true;
    in.fog_color = {0.1f, 0.2f, 0.3f};
    in.fog_strength = 0.5f;
    in.texture_filter = 3;
    in.aniso_level = 32;
    in.dither = true;
    in.window_width = 320;
    in.foliage_edge = 2;
    in.fog.params = {1.f, 2.f, 3.f, 4.f};

    gpu::SplitDraw d = gpu::plan_split(prims(Blend::None, 0, 3), v, in, ctx);
    SH_CHECK(d.vs.projection == ctx.projection);
    SH_CHECK(d.vs.pgxp[0] == 1.f && d.vs.pgxp[1] == 1234.f);
    SH_CHECK(d.vs.modes[0] == 1.f);
    SH_CHECK(d.fog.params[3] == 4.f);
    SH_CHECK(d.fs.fog[2] == 0.3f && d.fs.fog[3] == 0.5f);
    SH_CHECK(d.fs.modes[1] == 1.f);
    SH_CHECK(d.fs.modes[2] == 1.f);   // bilinear on
    SH_CHECK(d.fs.modes[3] == 16.f);  // aniso taps capped
    SH_CHECK(d.fs.dither[0] == 1.f && d.fs.dither[1] == 4.f && d.fs.dither[2] == 0.f && d.fs.dither[3] == 480.f);
    SH_CHECK(d.fs.rgbaFb[2] == 1.f);
    SH_CHECK(d.fs.rgbaFb[3] == 1.f);  // foliage_edge 2 falls back to 1 without samples
    SH_CHECK(d.fs.prog[0] == 0.f);

    // Blended prims have no foliage edge; glyphs are point sampled and fully sharp.
    Split glyph = prims(Blend::Average, 0, 3);
    glyph.glyph = true;
    d = gpu::plan_split(glyph, v, in, ctx);
    SH_CHECK(d.fs.rgbaFb[3] == 0.f);
    SH_CHECK(d.fs.modes[2] == 0.f);
    SH_CHECK(d.fs.rgbaFb[2] == 2.f);

    in.dither_suppressed = true;
    in.menu_filter = true;
    d = gpu::plan_split(prims(Blend::None, 0, 3), v, in, ctx);
    SH_CHECK(d.fs.modes[2] == 2.f);
    SH_CHECK(d.fs.dither[0] == 0.f);

    // The override program: aniso off, footprint terms of the hi-res texture.
    Split ovr = prims(Blend::None, 0, 3);
    ovr.format = gpu::TexFormat::Rgba32;
    ovr.override_info.width = 64;
    ovr.override_info.height = 32;
    ovr.override_info.offset_x = 8;
    d = gpu::plan_split(ovr, v, in, ctx);
    SH_CHECK(d.rgba);
    SH_CHECK(d.fs.prog[0] == 1.f);
    SH_CHECK(d.fs.modes[3] == 1.f);
    SH_CHECK(std::fabs(d.fs.rgbaTex[0] - 1.f / 64.f) < 1e-7f);
    SH_CHECK(std::fabs(d.fs.rgbaTex[1] - 1.f / 32.f) < 1e-7f);
    SH_CHECK(d.fs.rgbaTex[2] == 8.f);

    // Shadows: only when wanted and a map exists.
    in.shadows = true;
    SH_CHECK(!gpu::plan_split(prims(Blend::None, 0, 3), v, in, ctx).shadow);
    ctx.shadow_map = true;
    d = gpu::plan_split(prims(Blend::None, 0, 3), v, in, ctx);
    SH_CHECK(d.shadow && d.fs.prog[3] == 1.f);
}

SH_TEST(gpu_split_flashlight_block) {
    gpu::ShadeInputs in;
    gpu::SplitContext ctx;
    const auto v = triangles(1);
    in.per_pixel_flashlight = true;
    in.flashlight_active = true;
    in.flashlight_size = 2.f;
    in.flashlight_inner_cos = 0.9f;
    in.flashlight_intensity = 2.f;
    in.flashlight_color = {0.5f, 1.f, 0.25f};
    in.flashlight_range = 1000.f;
    const gpu::SplitDraw d = gpu::plan_split(prims(Blend::None, 0, 3), v, in, ctx);
    SH_CHECK(d.fs.flMode[0] == 1.f && d.fs.flMode[1] == 0.f && d.fs.flMode[3] == 1000.f);
    SH_CHECK(std::fabs(d.fs.flPos[3] - 0.8f) < 1e-6f);   // 1 - 2 * (1 - 0.9)
    SH_CHECK(std::fabs(d.fs.flDir[3] - 0.64f) < 1e-6f);  // classic: 1 - 2 * (1 - 0.82)
    SH_CHECK(d.fs.flColor[0] == 1.f && d.fs.flColor[1] == 2.f && d.fs.flColor[2] == 0.5f);
}

SH_TEST(gpu_split_order_plain_and_item_pass) {
    gpu::SplitBatch b;
    b.vertices = triangles(4, 0.f, 1.f);
    b.splits.push_back(prims(Blend::Average, 0, 6));  // far triangles 0, 1
    b.splits.push_back(prims(Blend::None, 6, 3));
    Split capture;
    capture.kind = Split::Kind::Capture;
    b.splits.push_back(capture);
    b.splits.push_back(prims(Blend::Average, 9, 3));

    auto steps = gpu::order_splits(b);
    SH_CHECK(steps.size() == 4);
    SH_CHECK(steps[0].split == 0 && !steps[0].item_pass);

    // Reverse the depths so the sort has work: triangle 0 nearest.
    b.vertices = triangles(4, 3.f, -1.f);
    b.force_item_depth = true;
    steps = gpu::order_splits(b);
    // Opaque split 1, then split 0's triangles back to front (smaller z first: vertex 3, then 0), then the capture
    // barrier, then split 3 (a run of its own with translucency: one triangle).
    SH_CHECK(steps.size() == 5);
    SH_CHECK(steps[0].split == 1 && steps[0].item_pass && steps[0].count == 3);
    SH_CHECK(steps[1].split == 0 && steps[1].first == 3 && steps[1].count == 3 && steps[1].item_pass);
    SH_CHECK(steps[2].split == 0 && steps[2].first == 0);
    SH_CHECK(steps[3].split == 2 && !steps[3].item_pass);
    SH_CHECK(steps[4].split == 3 && steps[4].item_pass);

    // A non-finite depth ends the run: the split is drawn as recorded.
    b.vertices[0].z = INFINITY;
    steps = gpu::order_splits(b);
    SH_CHECK(steps[0].split == 0 && !steps[0].item_pass && steps[0].count == 6);
}

SH_TEST(gpu_split_issue_on_fake_device) {
    FakeDevice dev;
    auto made = rhi::PrimRenderer::create(dev, rhi::TextureFormat::RGBA8, true, 1024);
    SH_CHECK(static_cast<bool>(made));
    if (!made) return;
    rhi::PrimRenderer& pr = *made.value();
    FakePass pass;
    pr.beginDraws(pass, {}, {});
    const std::size_t base_samplers = pass.samplers.size();

    gpu::ShadeInputs in;
    gpu::SplitContext ctx;
    const auto v = triangles(2);
    gpu::BoundTextures bound;
    bound.vram = pr.vram();

    gpu::SplitBindings tex;
    tex.vram = pr.vram();
    gpu::SplitDraw d = gpu::plan_split(prims(Blend::AddQuarter, 0, 6), v, in, ctx);
    SH_CHECK(static_cast<bool>(gpu::issue_split(pass, pr, d, tex, bound)));
    SH_CHECK(pass.samplers.size() == base_samplers);  // VRAM already bound
    SH_CHECK(pass.draws.size() == 1 && pass.draws[0].first == 0 && pass.draws[0].second == 6);
    SH_CHECK(pass.blend[3] == 0.25f);
    SH_CHECK(dev.pipelines.size() == 1);
    SH_CHECK(dev.pipelines[0].blend.enable && dev.pipelines[0].blend.srcColor == rhi::BlendFactor::ConstantColor);
    SH_CHECK(dev.pipelines[0].depthStencil.stencilTest);
    SH_CHECK(pass.last_fs.modes[0] == 1.f);  // fog to black for the additive split

    // An override split binds its texture to the RGBA slot once; the same key reuses the pipeline.
    Split ovr = prims(Blend::AddQuarter, 3, 3);
    ovr.format = gpu::TexFormat::Rgba32;
    d = gpu::plan_split(ovr, v, in, ctx);
    tex.rgba = rhi::TextureH{999};
    SH_CHECK(static_cast<bool>(gpu::issue_split(pass, pr, d, tex, bound)));
    SH_CHECK(static_cast<bool>(gpu::issue_split(pass, pr, d, tex, bound)));
    SH_CHECK(pass.samplers.size() == base_samplers + 1);
    SH_CHECK(pass.samplers.back().slot == rhi::kPrimSamplerRgba && pass.samplers.back().texture == 999);
    SH_CHECK(dev.pipelines.size() == 1);
    SH_CHECK(pass.last_fs.prog[0] == 1.f);
    SH_CHECK(pass.draws.size() == 3);

    // A skipped split draws nothing.
    d = gpu::plan_split(prims(Blend::Add, 0, 0), v, in, ctx);
    SH_CHECK(static_cast<bool>(gpu::issue_split(pass, pr, d, tex, bound)));
    SH_CHECK(pass.draws.size() == 3);
}
