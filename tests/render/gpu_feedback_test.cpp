// sh::gpu::FeedbackLogic and FeedbackGpu (render/gpu/feedback_pass.h): when the frame is stored into the
// display-buffer rects, blanked, repacked, captured for the scene scratch; which capture a feedback sprite reads; and
// the passes recorded on a fake device.

#include "framework/sh_test.h"
#include "render/gpu/feedback_pass.h"
#include "render/gpu/shaders/feedback_shaders.h"

#include <cstring>
#include <vector>

namespace {

using namespace sh;
using gpu::CaptureSlot;
using gpu::FeedbackLogic;
using gpu::FeedbackView;
using gpu::PackMode;

gpu::Rect rect(int x, int y, int w, int h) {
    return {static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(w),
            static_cast<std::int16_t>(h)};
}

// A 1280x960 scene target whose world pass recorded the display buffer at (160,0)-(1120,960) (GL bottom-left).
FeedbackView view() {
    FeedbackView v;
    v.world_area = {160.f, 0.f, 1120.f, 960.f};
    v.world_area_valid = true;
    v.present_viewport = {0, 0, 1280, 960};
    v.target_w = 1280;
    v.target_h = 960;
    return v;
}

FeedbackLogic with_buffers() {
    FeedbackLogic l;
    l.set_display_buffers(0, 32, 0, 256, 320, 224);
    return l;
}

// ---- fake device ----------------------------------------------------------------------------------------------

struct Record {
    std::vector<rhi::RenderPassDesc> passes;
    std::vector<rhi::Viewport> viewports;
    std::vector<std::vector<std::byte>> uniforms;
    struct Copy {
        rhi::TextureH src, dst;
        rhi::Rect src_rect;
        int x, y;
    };
    std::vector<Copy> copies;
    struct Upload {
        rhi::TextureH tex;
        rhi::Rect rect;
        std::size_t bytes;
    };
    std::vector<Upload> uploads;
    struct Blit {
        rhi::TextureH src, dst;
        rhi::Rect src_rect, dst_rect;
    };
    std::vector<Blit> blits;
    int draws = 0;
};

class FakePass final : public rhi::RenderPass {
public:
    explicit FakePass(Record& r) : r_(r) {}
    void bindPipeline(rhi::PipelineH) override {}
    void setViewport(const rhi::Viewport& v) override { r_.viewports.push_back(v); }
    void setScissor(const rhi::Rect&) override {}
    void setBlendConstants(const std::array<float, 4>&) override {}
    void setStencilReference(std::uint8_t) override {}
    void bindVertexBuffer(rhi::BufferH, std::uint32_t) override {}
    void bindIndexBuffer(rhi::BufferH, rhi::IndexType, std::uint32_t) override {}
    void bindSampler(rhi::ShaderStage, std::uint32_t, rhi::TextureH, rhi::SamplerH) override {}
    void pushUniforms(rhi::ShaderStage, std::uint32_t, std::span<const std::byte> d) override {
        r_.uniforms.emplace_back(d.begin(), d.end());
    }
    void draw(std::uint32_t, std::uint32_t) override { r_.draws++; }
    void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}

private:
    Record& r_;
};

class FakeCmd final : public rhi::CommandList {
public:
    explicit FakeCmd(Record& r) : r_(r), pass_(r) {}
    void uploadTexture(rhi::TextureH t, const rhi::Rect& rc, std::span<const std::byte> px) override {
        r_.uploads.push_back({t, rc, px.size()});
    }
    void uploadBuffer(rhi::BufferH, std::uint32_t, std::span<const std::byte>) override {}
    void copyTexture(rhi::TextureH s, const rhi::Rect& sr, rhi::TextureH d, std::int32_t x, std::int32_t y) override {
        r_.copies.push_back({s, d, sr, x, y});
    }
    void blitTexture(rhi::TextureH s, const rhi::Rect& sr, rhi::TextureH d, const rhi::Rect& dr, rhi::Filter) override {
        r_.blits.push_back({s, d, sr, dr});
    }
    void resolveTexture(rhi::TextureH, rhi::TextureH) override {}
    rhi::RenderPass* beginRenderPass(const rhi::RenderPassDesc& d) override {
        r_.passes.push_back(d);
        return &pass_;
    }
    void endRenderPass() override {}
    rhi::TextureH acquireSwapchainTexture(std::uint32_t*, std::uint32_t*) override { return {}; }

private:
    Record& r_;
    FakePass pass_;
};

class FakeDevice final : public rhi::Device {
public:
    explicit FakeDevice(rhi::Backend b = rhi::Backend::Gl33) : backend_(b), cmd(rec) {}
    Record rec;
    std::vector<rhi::TextureDesc> textures;
    bool fail_shaders = false;

    rhi::Backend backend() const override { return backend_; }
    const rhi::Caps& caps() const override { return caps_; }
    Result<rhi::TextureH> createTexture(const rhi::TextureDesc& d) override {
        textures.push_back(d);
        return Result<rhi::TextureH>::ok(rhi::TextureH{static_cast<std::uint32_t>(textures.size())});
    }
    Result<rhi::BufferH> createBuffer(const rhi::BufferDesc&) override { return Result<rhi::BufferH>::ok({1}); }
    Result<rhi::SamplerH> createSampler(const rhi::SamplerDesc&) override {
        return Result<rhi::SamplerH>::ok({++samplers_});
    }
    Result<rhi::ShaderH> createShader(const rhi::ShaderDesc&) override {
        if (fail_shaders) return Result<rhi::ShaderH>::fail("no shaders");
        return Result<rhi::ShaderH>::ok({++shaders_});
    }
    Result<rhi::PipelineH> createPipeline(const rhi::PipelineDesc&) override {
        return Result<rhi::PipelineH>::ok({++pipelines_});
    }
    void destroy(rhi::TextureH) override {}
    void destroy(rhi::BufferH) override {}
    void destroy(rhi::SamplerH) override {}
    void destroy(rhi::ShaderH) override {}
    void destroy(rhi::PipelineH) override {}
    rhi::CommandList* begin() override { return &cmd; }
    Result<void> submit() override { return Result<void>::ok(); }
    Result<void> readback(rhi::TextureH, const rhi::Rect&, std::span<std::byte>) override {
        return Result<void>::fail("no");
    }
    void waitIdle() override {}

    const rhi::TextureDesc& desc(rhi::TextureH h) const { return textures.at(h.raw - 1); }

private:
    rhi::Backend backend_;
    rhi::Caps caps_;
    std::uint32_t samplers_ = 0, shaders_ = 0, pipelines_ = 0;

public:
    FakeCmd cmd;
};

template <class T>
T uniforms_at(const Record& r, std::size_t i) {
    T t{};
    std::memcpy(&t, r.uniforms.at(i).data(), sizeof(T));
    return t;
}

}  // namespace

// ---- decisions ------------------------------------------------------------------------------------------------

SH_TEST(gpu_feedback_no_display_buffers_does_nothing) {
    FeedbackLogic l;
    l.note_sampler(false, 128, false);
    const auto plan = l.plan_store(view());
    SH_CHECK(plan.empty());
    SH_CHECK(plan.tick_redirect);
}

SH_TEST(gpu_feedback_no_reader_blanks_once) {
    FeedbackLogic l = with_buffers();
    auto plan = l.plan_store(view());
    SH_CHECK(plan.captures.empty() && plan.packs.empty());
    SH_CHECK(plan.clears.size() == 2);
    SH_CHECK(plan.clears[0] == rect(0, 32, 320, 224) && plan.clears[1] == rect(0, 256, 320, 224));
    // Still blank: no repeated clears.
    SH_CHECK(l.plan_store(view()).clears.empty());
    // Something wrote over a rect: blank again.
    l.note_texture_write(rect(10, 40, 4, 4));
    SH_CHECK(l.plan_store(view()).clears.size() == 2);
    // A write elsewhere keeps it.
    l.note_texture_write(rect(600, 0, 64, 64));
    SH_CHECK(l.plan_store(view()).clears.empty());
    // A live scene redirect is one more rect to blank.
    FeedbackView v = view();
    v.redirect = rect(320, 256, 320, 224);
    plan = l.plan_store(v);
    SH_CHECK(plan.clears.size() == 3 && plan.clears[2] == rect(320, 256, 320, 224));
}

SH_TEST(gpu_feedback_opaque_reader_exact_screen_capture) {
    FeedbackLogic l = with_buffers();
    l.settings().damp = 0.75f;
    l.note_sampler(false, 128, false);
    SH_CHECK(l.store_allowed() == 2);
    const auto plan = l.plan_store(view());
    SH_CHECK(plan.captures.size() == 1);
    const auto& c = plan.captures[0];
    SH_CHECK(c.slot == CaptureSlot::Pack && c.w == 960 && c.h == 960 && c.nearest);
    SH_CHECK(c.blit.valid && c.blit.src.x0 == 160.f && c.blit.src.x1 == 1120.f && c.blit.dx0 == 0 && c.blit.dx1 == 960);
    SH_CHECK(l.screen_valid());
    SH_CHECK(l.screen_origin()[0] == 160.f && l.screen_origin()[1] == 0.f);
    SH_CHECK(plan.packs.size() == 2);
    for (const auto& p : plan.packs) {
        SH_CHECK(p.source == CaptureSlot::Pack);
        SH_CHECK(p.params.mode == PackMode::Exact && !p.params.mask && !p.params.decay && p.params.gain == 0.75f);
    }
    SH_CHECK(plan.packs[1].rect == rect(0, 256, 320, 224));
    SH_CHECK(l.store_allowed() == 1);
    // One more store from the trailing window, then the rects are blanked.
    SH_CHECK(l.plan_store(view()).captures.size() == 1);
    const auto off = l.plan_store(view());
    SH_CHECK(off.captures.empty() && off.clears.size() == 2);
    SH_CHECK(!l.screen_valid());
}

SH_TEST(gpu_feedback_reader_kinds) {
    FeedbackLogic l = with_buffers();
    l.settings().damp_blend = 0.5f;
    l.note_sampler(false, 127, false);  // a decay frame of the loading trail
    auto plan = l.plan_store(view());
    SH_CHECK(plan.packs[0].params.mode == PackMode::Exact && plan.packs[0].params.decay);

    l.note_sampler(true, 127, false);  // a blending dream overlay
    plan = l.plan_store(view());
    SH_CHECK(plan.packs[0].params.mode == PackMode::Scaled && plan.packs[0].params.mask);
    SH_CHECK(!plan.packs[0].params.decay && plan.packs[0].params.gain == 0.5f);

    l.settings().exact = false;
    l.note_sampler(false, 128, false);
    plan = l.plan_store(view());
    SH_CHECK(plan.packs[0].params.mode == PackMode::Scaled && !plan.packs[0].params.mask);
}

SH_TEST(gpu_feedback_screen_off_captures_buffer_size) {
    FeedbackLogic l = with_buffers();
    l.settings().screen = false;
    l.note_sampler(true, 128, false);
    const auto plan = l.plan_store(view());
    SH_CHECK(plan.captures.size() == 1);
    SH_CHECK(plan.captures[0].w == 320 && plan.captures[0].h == 224);
    SH_CHECK(!plan.captures[0].nearest);  // a blending reader keeps the filtered downscale
    SH_CHECK(!l.screen_valid());
}

SH_TEST(gpu_feedback_hold_skip_and_suppress) {
    FeedbackLogic l = with_buffers();
    l.note_sampler(false, 128, false);
    l.set_hold_frame();
    auto plan = l.plan_store(view());
    SH_CHECK(plan.empty() && l.store_allowed() == 1);
    // The hold is consumed.
    SH_CHECK(l.plan_store(view()).captures.size() == 1);

    l.note_sampler(false, 128, false);
    l.set_skip_store(true);
    SH_CHECK(l.plan_store(view()).empty());
    SH_CHECK(!l.skip_store());

    // The game uploads into a display buffer: four stores stand down.
    l.captured(CaptureSlot::Pack, 960, 960);
    SH_CHECK(l.note_upload(rect(0, 40, 16, 16)));
    SH_CHECK(!l.note_upload(rect(640, 0, 16, 16)));
    for (int i = 0; i < 4; ++i) {
        l.note_sampler(false, 128, false);
        SH_CHECK(l.plan_store(view()).empty());
    }
    l.note_sampler(false, 128, false);
    SH_CHECK(l.plan_store(view()).captures.size() == 1);
}

SH_TEST(gpu_feedback_redirect_is_packed_too) {
    FeedbackLogic l = with_buffers();
    FeedbackView v = view();
    v.redirect = rect(320, 256, 320, 224);
    l.note_sampler(true, 128, false);
    const auto plan = l.plan_store(v);
    SH_CHECK(plan.packs.size() == 3 && plan.packs[2].rect == rect(320, 256, 320, 224));
}

SH_TEST(gpu_feedback_scratch_capture) {
    FeedbackLogic l = with_buffers();
    FeedbackView v = view();
    v.redirect = rect(320, 256, 320, 224);
    auto plan = l.plan_scratch_capture(v, rect(320, 256, 320, 224));
    SH_CHECK(plan.captures.size() == 1 && plan.captures[0].slot == CaptureSlot::Scratch);
    SH_CHECK(plan.captures[0].w == 960 && plan.captures[0].nearest);
    SH_CHECK(plan.packs.size() == 1 && plan.packs[0].source == CaptureSlot::Scratch);
    SH_CHECK(plan.packs[0].params.mask && plan.packs[0].params.gain == 1.f &&
             plan.packs[0].params.mode == PackMode::Scaled);
    SH_CHECK(l.scratch_valid());
    SH_CHECK(!plan.tick_redirect);
    // The capture serves its own frame only.
    l.plan_store(v);
    SH_CHECK(!l.scratch_valid());

    // World ortho wider than the buffer: the capture spans the viewport's width.
    v.world_wide = true;
    plan = l.plan_scratch_capture(v, rect(320, 256, 320, 224));
    SH_CHECK(plan.captures[0].w == 1280);

    // Without the render-resolution capture: the rect size, filtered, from the pack slot.
    l.settings().screen = false;
    plan = l.plan_scratch_capture(v, rect(320, 256, 320, 224));
    SH_CHECK(plan.captures[0].slot == CaptureSlot::Pack && plan.captures[0].w == 320 && !plan.captures[0].nearest);
    SH_CHECK(plan.packs[0].source == CaptureSlot::Pack && !l.scratch_valid());

    l.settings().dream_feedback = false;
    SH_CHECK(l.plan_scratch_capture(v, rect(320, 256, 320, 224)).empty());
    SH_CHECK(l.plan_scratch_capture(v, rect(0, 0, 0, 0)).empty());
}

SH_TEST(gpu_feedback_repack_after_upload) {
    FeedbackLogic l = with_buffers();
    // Nothing captured yet, a reader active: nothing to restore.
    l.note_sampler(false, 127, false);
    SH_CHECK(l.plan_repack(view(), rect(0, 32, 320, 224)).empty());

    l.plan_store(view());
    l.captured(CaptureSlot::Pack, 960, 960);
    // The reader changes this frame; the repack restores what the store wrote.
    l.note_sampler(true, 128, false);
    SH_CHECK(l.plan_repack(view(), rect(600, 0, 32, 32)).empty());
    auto plan = l.plan_repack(view(), rect(100, 100, 32, 32));
    SH_CHECK(plan.captures.empty() && plan.packs.size() == 2);
    SH_CHECK(plan.packs[0].params.mode == PackMode::Exact && plan.packs[0].params.decay && !plan.packs[0].params.mask);

    // No reader any more: the upload is blanked over instead.
    l.plan_store(view());
    l.plan_store(view());
    SH_CHECK(l.store_allowed() == 0);
    l.plan_store(view());  // blanks
    plan = l.plan_repack(view(), rect(100, 100, 32, 32));
    SH_CHECK(plan.packs.empty() && plan.clears.size() == 2);

    l.set_skip_store(true);
    SH_CHECK(l.plan_repack(view(), rect(100, 100, 32, 32)).empty());
}

SH_TEST(gpu_feedback_sample_selection) {
    FeedbackLogic l = with_buffers();
    render::feedback::SampleQuery q;
    q.pageX = 0;
    q.pageY = 256;
    q.u0 = 0;
    q.v0 = 0;
    q.dstX = 0;
    q.dstY = 0;
    q.w = 320;
    q.h = 224;
    SH_CHECK(l.sample(view(), q).result.kind == render::feedback::SampleKind::None);

    l.note_sampler(false, 128, false);
    l.plan_store(view());
    l.captured(CaptureSlot::Pack, 960, 960);
    auto s = l.sample(view(), q);
    SH_CHECK(s.result.kind == render::feedback::SampleKind::Screen && s.slot == CaptureSlot::Pack);
    SH_CHECK(s.result.mode == 1 && s.result.natW == 320 && s.result.natH == 224);
    q.dstX = 8;  // a shifted copy samples through its UVs
    SH_CHECK(l.sample(view(), q).result.mode == 2);

    FeedbackView v = view();
    v.redirect = rect(320, 256, 320, 224);
    l.plan_scratch_capture(v, rect(320, 256, 320, 224));
    l.captured(CaptureSlot::Scratch, 960, 960);
    render::feedback::SampleQuery sq;
    sq.scratch = true;
    sq.pageX = 320;
    sq.pageY = 256;
    sq.w = 64;
    sq.h = 64;
    s = l.sample(v, sq);
    SH_CHECK(s.result.kind == render::feedback::SampleKind::Scratch && s.slot == CaptureSlot::Scratch);
    // The redirect lapsed: back to video memory.
    SH_CHECK(l.sample(view(), sq).result.kind == render::feedback::SampleKind::None);
}

SH_TEST(gpu_feedback_shader_inputs) {
    FeedbackLogic l = with_buffers();
    l.note_sampler(false, 128, false);
    l.plan_store(view());
    l.captured(CaptureSlot::Pack, 960, 480);
    rhi::PrimFsUniforms fs;
    l.shader_inputs(&fs, 1 | 4);
    SH_CHECK(fs.prog[1] == 1.f && fs.prog[2] == 1.f);
    SH_CHECK(fs.rgbaHalf[2] == 160.f && fs.rgbaHalf[3] == 0.f);
    SH_CHECK(fs.rgbaFb[0] == 1.f / 960.f && fs.rgbaFb[1] == 1.f / 480.f);
    l.shader_inputs(&fs, 0);
    SH_CHECK(fs.prog[1] == 0.f && fs.prog[2] == 0.f);
}

// ---- passes on a fake device ----------------------------------------------------------------------------------

SH_TEST(gpu_feedback_pack_records_pass_and_copy) {
    FakeDevice dev;
    gpu::FeedbackGpu g(dev);
    gpu::PackParams p;
    p.mode = PackMode::Exact;
    p.gain = 1.f;
    const rhi::TextureH src{77}, vram{99};
    SH_CHECK(g.pack(dev.cmd, src, 960, 960, {0, 0, 960, 960}, true, vram, rect(0, 256, 320, 224), p));
    const Record& r = dev.rec;
    SH_CHECK(r.passes.size() == 1);
    const rhi::TextureH target = r.passes[0].color.texture;
    SH_CHECK(dev.desc(target).format == rhi::TextureFormat::RG8 && dev.desc(target).width == 1024 &&
             dev.desc(target).height == 512);
    SH_CHECK((dev.desc(target).usage & rhi::kUsageColorTarget) != 0);
    // GL: the rect sits at the bottom of the target in RHI terms, storage rows 0..h-1.
    SH_CHECK(r.viewports.size() == 1 && r.viewports[0].y == 512.f - 224.f && r.viewports[0].w == 320.f);
    SH_CHECK(r.copies.size() == 1 && r.copies[0].src == target && r.copies[0].dst == vram);
    SH_CHECK(r.copies[0].x == 0 && r.copies[0].y == 256 && r.copies[0].src_rect.w == 320 &&
             r.copies[0].src_rect.h == 224 && r.copies[0].src_rect.y == 0);
    struct U {
        float src[4], info[4], dst[4], params[4];
    };
    const U u = uniforms_at<U>(r, 0);
    SH_CHECK(u.info[0] == 960.f && u.info[2] == 1.f && u.dst[0] == 320.f && u.dst[1] == 224.f);
    SH_CHECK(u.params[0] == 1.f && u.params[1] == 0.f && u.params[2] == 1.f && u.params[3] == 0.f);

    // Outside video memory: refused.
    SH_CHECK(!g.pack(dev.cmd, src, 960, 960, {0, 0, 960, 960}, true, vram, rect(900, 0, 320, 224), p));
    SH_CHECK(r.copies.size() == 1);
}

SH_TEST(gpu_feedback_capture_records_pass) {
    FakeDevice dev;
    gpu::FeedbackGpu g(dev);
    FeedbackLogic l = with_buffers();
    l.note_sampler(false, 128, false);
    const auto plan = l.plan_store(view());
    SH_CHECK(g.capture(dev.cmd, rhi::TextureH{55}, 1280, 960, plan.captures[0]));
    const Record& r = dev.rec;
    SH_CHECK(r.passes.size() == 1 && r.passes[0].color.load == rhi::LoadOp::Clear);
    const rhi::TextureH cap = g.capture_texture(CaptureSlot::Pack);
    SH_CHECK(cap == r.passes[0].color.texture && dev.desc(cap).width == 960 && dev.desc(cap).height == 960);
    struct U {
        float src[4], dst[4], size[4];
    };
    const U u = uniforms_at<U>(r, 0);
    SH_CHECK(u.src[0] == 160.f && u.src[2] == 1120.f && u.dst[2] == 960.f && u.dst[3] == 960.f);
    SH_CHECK(u.size[0] == 1280.f && u.size[1] == 960.f && u.size[2] == 0.f);
    int w = 0, h = 0;
    g.capture_size(CaptureSlot::Pack, &w, &h);
    SH_CHECK(w == 960 && h == 960);
    // pack_from reads the capture bottom up.
    gpu::PackParams p;
    SH_CHECK(g.pack_from(dev.cmd, CaptureSlot::Pack, rhi::TextureH{99}, rect(0, 32, 320, 224), p));
    SH_CHECK(!g.pack_from(dev.cmd, CaptureSlot::Scratch, rhi::TextureH{99}, rect(0, 32, 320, 224), p));
}

SH_TEST(gpu_feedback_sdlgpu_orientation) {
    if (rhi::embedded::gpu_feedback_pack_frag().spirv.empty()) SH_SKIP("no SPIR-V in this build");
    FakeDevice dev(rhi::Backend::SdlGpu);
    gpu::FeedbackGpu g(dev);
    gpu::PackParams p;
    SH_CHECK(g.pack(dev.cmd, rhi::TextureH{77}, 320, 224, {0, 0, 320, 224}, false, rhi::TextureH{99},
                    rect(0, 32, 320, 224), p));
    SH_CHECK(dev.rec.viewports[0].y == 0.f);
    gpu::CaptureStep c;
    c.w = 64;
    c.h = 64;
    SH_CHECK(g.capture(dev.cmd, rhi::TextureH{55}, 640, 480, c));
    struct U {
        float src[4], dst[4], size[4];
    };
    SH_CHECK(uniforms_at<U>(dev.rec, 1).size[2] == 1.f);
}

SH_TEST(gpu_feedback_clear_and_failures) {
    FakeDevice dev;
    dev.fail_shaders = true;
    gpu::FeedbackGpu g(dev);
    SH_CHECK(!g.pipelines_ready());
    gpu::PackParams p;
    SH_CHECK(!g.pack(dev.cmd, rhi::TextureH{77}, 320, 224, {0, 0, 320, 224}, true, rhi::TextureH{99},
                     rect(0, 32, 320, 224), p));
    SH_CHECK(dev.rec.passes.empty());
    // Blanking needs no pipeline: word 0 uploaded over the rect.
    g.clear(dev.cmd, rhi::TextureH{99}, rect(0, 256, 320, 224));
    SH_CHECK(dev.rec.uploads.size() == 1 && dev.rec.uploads[0].bytes == 320u * 224u * 2u);
    SH_CHECK(dev.rec.uploads[0].rect.y == 256 && dev.rec.uploads[0].tex == rhi::TextureH{99});
}

SH_TEST(gpu_feedback_freeze_frame) {
    FakeDevice dev;
    gpu::FeedbackGpu g(dev);
    const rhi::TextureH scene{55};
    SH_CHECK(!g.present_last_frame(dev.cmd, scene, 640, 480));
    g.capture_last_frame(dev.cmd, scene, rhi::TextureFormat::RGBA8, 640, 480, false);
    SH_CHECK(dev.rec.blits.size() == 1 && dev.rec.blits[0].src == scene && g.freeze().valid());
    SH_CHECK(g.present_last_frame(dev.cmd, scene, 640, 480));
    SH_CHECK(dev.rec.blits.size() == 2 && dev.rec.blits[1].dst == scene);
    // The frame that re-presented the capture is not captured again.
    g.capture_last_frame(dev.cmd, scene, rhi::TextureFormat::RGBA8, 640, 480, false);
    SH_CHECK(dev.rec.blits.size() == 2);
    // Held (pause sub-screens): the frozen frame stays.
    g.capture_last_frame(dev.cmd, scene, rhi::TextureFormat::RGBA8, 640, 480, true);
    SH_CHECK(dev.rec.blits.size() == 2);
    g.capture_last_frame(dev.cmd, scene, rhi::TextureFormat::RGBA8, 640, 480, false);
    SH_CHECK(dev.rec.blits.size() == 3);
    // A resize drops the old picture until the next capture.
    g.capture_last_frame(dev.cmd, scene, rhi::TextureFormat::RGBA8, 800, 600, false);
    SH_CHECK(g.freeze().width() == 800 && g.freeze().valid());
}
