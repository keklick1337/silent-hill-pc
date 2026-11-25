// sh::gpu::Targets (render/gpu/targets.h): what each split draws into and with which viewport, scissor and
// projection; internal resolution, present rect and the window -> picture mapping. A recording fake device stands in
// for the RHI.

#include "framework/sh_test.h"
#include "render/frame/frame.h"
#include "render/gpu/targets.h"
#include "render/present/present_math.h"

#include <cmath>
#include <utility>
#include <vector>

namespace {

using namespace sh;

class FakePass final : public rhi::RenderPass {
public:
    void bindPipeline(rhi::PipelineH) override {}
    void setViewport(const rhi::Viewport&) override {}
    void setScissor(const rhi::Rect&) override {}
    void setBlendConstants(const std::array<float, 4>&) override {}
    void setStencilReference(std::uint8_t) override {}
    void bindVertexBuffer(rhi::BufferH, std::uint32_t) override {}
    void bindIndexBuffer(rhi::BufferH, rhi::IndexType, std::uint32_t) override {}
    void bindSampler(rhi::ShaderStage, std::uint32_t, rhi::TextureH, rhi::SamplerH) override {}
    void pushUniforms(rhi::ShaderStage, std::uint32_t, std::span<const std::byte>) override {}
    void draw(std::uint32_t, std::uint32_t) override {}
    void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}
};

class FakeCmd final : public rhi::CommandList {
public:
    struct Blit {
        rhi::TextureH src, dst;
        rhi::Rect dst_rect;
    };
    std::vector<rhi::RenderPassDesc> passes;
    std::vector<Blit> blits;
    std::vector<std::pair<rhi::TextureH, rhi::TextureH>> resolves;
    std::vector<std::byte> last_upload;

    void uploadTexture(rhi::TextureH, const rhi::Rect&, std::span<const std::byte> px) override
    {
        last_upload.assign(px.begin(), px.end());
    }
    void uploadBuffer(rhi::BufferH, std::uint32_t, std::span<const std::byte>) override {}
    void copyTexture(rhi::TextureH, const rhi::Rect&, rhi::TextureH, std::int32_t, std::int32_t) override {}
    void blitTexture(rhi::TextureH src, const rhi::Rect&, rhi::TextureH dst, const rhi::Rect& r, rhi::Filter) override
    {
        blits.push_back({src, dst, r});
    }
    void resolveTexture(rhi::TextureH ms, rhi::TextureH dst) override { resolves.push_back({ms, dst}); }
    rhi::RenderPass* beginRenderPass(const rhi::RenderPassDesc& d) override
    {
        passes.push_back(d);
        return &pass_;
    }
    void endRenderPass() override {}
    rhi::TextureH acquireSwapchainTexture(std::uint32_t*, std::uint32_t*) override { return {}; }

private:
    FakePass pass_;
};

class FakeDevice final : public rhi::Device {
public:
    struct Tex {
        rhi::TextureDesc desc;
        bool alive = true;
    };
    std::vector<Tex> textures;
    FakeCmd cmd;

    rhi::Backend backend() const override { return rhi::Backend::Gl33; }
    const rhi::Caps& caps() const override { return caps_; }
    Result<rhi::TextureH> createTexture(const rhi::TextureDesc& d) override
    {
        textures.push_back({d, true});
        return Result<rhi::TextureH>::ok(rhi::TextureH{static_cast<std::uint32_t>(textures.size())});
    }
    Result<rhi::BufferH> createBuffer(const rhi::BufferDesc&) override { return Result<rhi::BufferH>::fail("no"); }
    Result<rhi::SamplerH> createSampler(const rhi::SamplerDesc&) override { return Result<rhi::SamplerH>::fail("no"); }
    Result<rhi::ShaderH> createShader(const rhi::ShaderDesc&) override { return Result<rhi::ShaderH>::fail("no"); }
    Result<rhi::PipelineH> createPipeline(const rhi::PipelineDesc&) override
    {
        return Result<rhi::PipelineH>::fail("no");
    }
    void destroy(rhi::TextureH h) override { textures.at(h.raw - 1).alive = false; }
    void destroy(rhi::BufferH) override {}
    void destroy(rhi::SamplerH) override {}
    void destroy(rhi::ShaderH) override {}
    void destroy(rhi::PipelineH) override {}
    rhi::CommandList* begin() override { return &cmd; }
    Result<void> submit() override { return Result<void>::ok(); }
    Result<void> readback(rhi::TextureH, const rhi::Rect&, std::span<std::byte>) override
    {
        return Result<void>::ok();
    }
    void waitIdle() override {}

    const rhi::TextureDesc& desc(rhi::TextureH h) const { return textures.at(h.raw - 1).desc; }
    void set_max_samples(std::uint32_t n) { caps_.maxSamples = n; }

private:
    rhi::Caps caps_;
};

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

gpu::DispEnv disp320x224()
{
    gpu::DispEnv d;
    d.disp = {0, 0, 320, 224};
    return d;
}

gpu::DrawEnv screen_env(std::int16_t x, std::int16_t y, std::int16_t w, std::int16_t h)
{
    gpu::DrawEnv e;
    e.clip = {x, y, w, h};
    e.draw_on_display = true;
    return e;
}

// Ortho L/R/T/B back from a column-major GL ortho matrix.
void ortho_of(const std::array<float, 16>& m, float* l, float* r, float* t, float* b)
{
    *l = (-1.0f - m[12]) / m[0];
    *r = (1.0f - m[12]) / m[0];
    *t = (1.0f - m[13]) / m[5];
    *b = (-1.0f - m[13]) / m[5];
}

// A target sized exactly w x h: render scale 1, windowed.
void sized(gpu::Targets& t, int w, int h) { t.resize(w, h); }

}  // namespace

SH_TEST(gpu_targets_on_screen_4_3) {
    FakeDevice dev;
    gpu::Targets t(dev);
    sized(t, 640, 480);
    SH_CHECK(t.width() == 640 && t.height() == 480);
    SH_CHECK(dev.desc(t.scene_color()).width == 640 && dev.desc(t.scene_depth()).format == rhi::TextureFormat::D24S8);

    const auto dt = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    SH_CHECK(dt.color == t.scene_color() && dt.depth_stencil == t.scene_depth());
    SH_CHECK(!dt.offscreen && !dt.offscreen_begin && !dt.offscreen_end);
    SH_CHECK(dt.viewport.x == 0 && dt.viewport.y == 0 && dt.viewport.w == 640 && dt.viewport.h == 480);
    SH_CHECK(!dt.scissor && dt.scissor_rect.w == 640 && dt.scissor_rect.h == 480);
    // The GL orthoProjection: x 0..320 left to right, y 0 at the top.
    SH_CHECK(near(dt.projection[0], 2.0f / 320.0f) && near(dt.projection[5], -2.0f / 224.0f));
    SH_CHECK(near(dt.projection[12], -1.0f) && near(dt.projection[13], 1.0f) && dt.projection[14] == 0.0f);
    SH_CHECK(dt.projection[10] == -1.0f && dt.projection[15] == 1.0f);
}

SH_TEST(gpu_targets_clipped_area_scissor_is_top_left) {
    FakeDevice dev;
    gpu::Targets t(dev);
    sized(t, 640, 480);
    // A 192-line band 16 lines down: bottom-left y 34, height 411 -> top-left y 480 - 34 - 411.
    const auto dt = t.for_split(screen_env(0, 16, 320, 192), disp320x224());
    SH_CHECK(dt.scissor);
    SH_CHECK(dt.scissor_rect.x == 0 && dt.scissor_rect.w == 640);
    SH_CHECK(dt.scissor_rect.h == 411 && dt.scissor_rect.y == 35);
    // Interlaced counts as clipped even for the full area.
    auto d = disp320x224();
    d.interlaced = true;
    SH_CHECK(t.for_split(screen_env(0, 0, 320, 224), d).scissor);
}

SH_TEST(gpu_targets_widescreen_hor_plus) {
    FakeDevice dev;
    gpu::Targets t(dev);
    sized(t, 1280, 720);
    frame::state().set_draw_offset(true, 0, 0);
    // World pass: hfov 0.76 and the CRT pixel aspect leave ~213.3 PSX units each side of the centre.
    auto dt = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    float l, r, top, b;
    ortho_of(dt.projection, &l, &r, &top, &b);
    SH_CHECK(near(l, 160.0f - 213.3333f) && near(r, 160.0f + 213.3333f) && near(top, 0.0f) && near(b, 224.0f));
    SH_CHECK(dt.viewport.x == 0 && dt.viewport.w == 1280 && dt.viewport.h == 720);
    SH_CHECK(t.latches().world_ortho_valid && near(t.latches().world_ortho[0], l));
    SH_CHECK(near(t.latches().world_ortho_rect[1], r));
    // The display buffer is the 320/426.67 core of the viewport.
    SH_CHECK(near(t.latches().area_vp[0], 160.0f, 0.05f) && near(t.latches().area_vp[2], 1120.0f, 0.05f));
    float a43 = 0, wide = 0;
    SH_CHECK(t.hor_plus_half_widths(&a43, &wide) && near(a43, 160.0f) && near(wide, 213.3333f));

    // A vertical crop keeps the top rows.
    t.view().world_vscale = 0.872f;
    dt = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    ortho_of(dt.projection, &l, &r, &top, &b);
    SH_CHECK(near(top, 0.0f) && near(b, 224.0f * 0.872f));

    // The UI pass has neither hfov nor the crop, and latches the HUD rect against the draw offset.
    t.view().ui_pass = true;
    frame::state().set_draw_offset(true, 160, 112);
    dt = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    ortho_of(dt.projection, &l, &r, &top, &b);
    SH_CHECK(near(b, 224.0f) && near(r - l, 426.6667f));
    SH_CHECK(near(t.latches().hud_rect[0], l - 160.0f) && near(t.latches().hud_rect[3], 224.0f - 112.0f));
    SH_CHECK(near(t.latches().feedback_wide_scale, 426.6667f / 320.0f));
    frame::state().set_draw_offset(false, 0, 0);

    // Pillarbox mode: 4:3 ortho in a centred 4:3 viewport.
    t.view().ui_pass = false;
    t.view().widescreen_mode = 0;
    dt = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    ortho_of(dt.projection, &l, &r, &top, &b);
    SH_CHECK(near(l, 0.0f) && near(r, 320.0f));
    SH_CHECK(dt.viewport.x == 160 && dt.viewport.w == 960 && dt.viewport.h == 720);
    SH_CHECK(!t.hor_plus_half_widths(&a43, &wide));
}

SH_TEST(gpu_targets_2d_screens) {
    FakeDevice dev;
    gpu::Targets t(dev);
    sized(t, 1280, 720);
    t.view().hor_plus = false;
    // A 2D screen spans the whole window: the ortho widens by the window's excess over 4:3, the 320 frame
    // stays centred at its aspect.
    SH_CHECK(t.ui2d_wide() && near(t.ui_wide_factor(), 4.0f / 3.0f));
    auto dt = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    float l, r, top, b;
    ortho_of(dt.projection, &l, &r, &top, &b);
    SH_CHECK(dt.viewport.x == 0 && dt.viewport.w == 1280);
    SH_CHECK(near(l, 160.0f - 160.0f * 4.0f / 3.0f) && near(r, 160.0f + 160.0f * 4.0f / 3.0f));
    // Its world pass stays inside the authored frame unless the screen laid itself out wide; the UI pass does not.
    SH_CHECK(dt.scissor && dt.scissor_rect.x == 160 && dt.scissor_rect.w == 960);
    t.view().ui_pass = true;
    dt = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    SH_CHECK(!dt.scissor);
    t.view().ui_pass = false;
    // A partial clip lands where its part of the centred frame is; a whole-display clip does not narrow it.
    dt = t.for_split(screen_env(0, 0, 160, 224), disp320x224());
    SH_CHECK(dt.scissor && dt.scissor_rect.x == 160 && dt.scissor_rect.w == 480);
    // Stretch mode (menu_pillarbox 0) stretches unless the screen asked for the width itself.
    t.view().menu_pillarbox = false;
    SH_CHECK(!t.ui2d_wide());
    t.view().ui2d_wide_frames = 1;
    SH_CHECK(t.ui2d_wide());

    // A window narrower than 4:3 letterboxes; odd gaps put the extra row above (bottom-left y rounds down).
    gpu::Targets n(dev);
    sized(n, 640, 641);
    n.view().hor_plus = false;
    dt = n.for_split(screen_env(0, 0, 320, 224), disp320x224());
    SH_CHECK(dt.viewport.w == 640 && dt.viewport.h == 480 && dt.viewport.y == 81);
    int x, y, w, h;
    n.picture_rect(&x, &y, &w, &h);
    SH_CHECK(y == 81 && h == 480);
    n.display_viewport(&x, &y, &w, &h);
    SH_CHECK(y == 80 && h == 480);  // bottom-left origin
}

SH_TEST(gpu_targets_offscreen_redirect) {
    FakeDevice dev;
    gpu::Targets t(dev);
    sized(t, 640, 480);
    gpu::DrawEnv off;
    off.clip = {320, 256, 64, 32};
    off.draw_on_display = false;

    auto dt = t.for_split(off, disp320x224());
    SH_CHECK(dt.offscreen && dt.offscreen_begin && !dt.offscreen_end);
    SH_CHECK(dt.color == t.offscreen_color() && !dt.depth_stencil);
    SH_CHECK(dev.desc(dt.color).width == 64 && dev.desc(dt.color).height == 32);
    SH_CHECK(dt.viewport.x == 0 && dt.viewport.y == 0 && dt.viewport.w == 64 && dt.viewport.h == 32);
    SH_CHECK(!dt.scissor);
    float l, r, top, b;
    ortho_of(dt.projection, &l, &r, &top, &b);
    SH_CHECK(near(l, 0.0f) && near(r, 64.0f) && near(top, 0.0f) && near(b, 32.0f));
    SH_CHECK(dt.offscreen_clear[0] == 0.5f && dt.offscreen_clear[3] == 0.0f);

    // Same area again: keep drawing, no new clear.
    dt = t.for_split(off, disp320x224());
    SH_CHECK(dt.offscreen && !dt.offscreen_begin && !dt.offscreen_end && t.offscreen_active());

    // Another area: the first one is written back, the new one starts clear.
    gpu::DrawEnv off2 = off;
    off2.clip = {0, 256, 32, 32};
    dt = t.for_split(off2, disp320x224());
    SH_CHECK(dt.offscreen_begin && dt.offscreen_end && dt.offscreen_writeback == off.clip);
    SH_CHECK(dev.desc(dt.color).width == 32);

    // Back on screen: the area is written back and the scene target is used.
    dt = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    SH_CHECK(!dt.offscreen && dt.offscreen_end && dt.offscreen_writeback == off2.clip);
    SH_CHECK(dt.color == t.scene_color() && !t.offscreen_active());
    dt = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    SH_CHECK(!dt.offscreen_end);
}

SH_TEST(gpu_targets_internal_resolution) {
    FakeDevice dev;
    gpu::Targets t(dev);
    t.view().render_scale = 0.5f;
    t.resize(1280, 720);
    SH_CHECK(t.width() == 640 && t.height() == 360 && t.present_width() == 1280);
    SH_CHECK(dev.desc(t.scene_color()).width == 640 && dev.desc(t.scene_color()).height == 360);
    const auto old = t.scene_color();
    t.resize(1280, 720);
    SH_CHECK(t.scene_color() == old);  // same size, same target

    // Borderless renders at the launcher's height, as wide as the desktop's shape.
    t.view().fullscreen_mode = 2;
    t.view().cfg_render_w = 800;
    t.view().cfg_render_h = 600;
    t.resize(1920, 1080);
    SH_CHECK(t.width() == 534 && t.height() == 300 && t.present_width() == 1920);
    SH_CHECK(!dev.textures.at(old.raw - 1).alive);

    // A 16:9 launcher size on a 3:2 desktop keeps the desktop's shape: the menus lay out for what is shown.
    t.view().render_scale = 1.0f;
    t.view().cfg_render_w = 1920;
    t.view().cfg_render_h = 1080;
    t.resize(2522, 1681);
    SH_CHECK(t.width() == 1620 && t.height() == 1080);
    SH_CHECK(near(t.ui_wide_factor(), (2522.0f / 1681.0f) / (4.0f / 3.0f)));
    int px, py, pw, ph;
    t.present_rect(&px, &py, &pw, &ph);
    SH_CHECK(px == 0 && py == 0 && pw == 2522 && ph == 1681);

    // A window the window manager resized: the target follows it whatever size was configured.
    t.view().fullscreen_mode = 0;
    t.resize(2522, 1673);
    SH_CHECK(t.width() == 2522 && t.height() == 1673);
    SH_CHECK(near(t.ui_wide_factor(), (2522.0f / 1673.0f) / (4.0f / 3.0f)));

    // Native follows the display mode, within the PS1's range.
    gpu::Targets n(dev);
    n.view().native_render = true;
    n.resize(1920, 1080);
    SH_CHECK(n.width() == 320 && n.height() == 240);
    n.track_native(disp320x224());
    SH_CHECK(n.width() == 320 && n.height() == 224);
    gpu::DispEnv tiny;
    tiny.disp = {0, 0, 200, 100};
    n.track_native(tiny);
    SH_CHECK(n.width() == 320 && n.height() == 224);
    SH_CHECK(!n.set_internal_resolution(0, 10) && !n.scene_color());
}

SH_TEST(gpu_targets_present_rect_and_pointer) {
    FakeDevice dev;
    gpu::Targets t(dev);
    t.view().hor_plus = false;
    // A 4:3 picture in a 16:9 window (a target that has not followed the window yet).
    t.resize(1920, 1080);
    SH_CHECK(t.set_internal_resolution(640, 480));
    int x, y, w, h;
    t.present_rect(&x, &y, &w, &h);
    SH_CHECK(x == 240 && y == 0 && w == 1440 && h == 1080);

    float fx = 0, fy = 0;
    SH_CHECK(t.window_to_viewport(240 + 720, 540, &fx, &fy) && near(fx, 0.5f) && near(fy, 0.5f));
    SH_CHECK(t.window_to_viewport(240, 0, &fx, &fy) && near(fx, 0.0f) && near(fy, 0.0f));
    SH_CHECK(!t.window_to_viewport(100, 540, &fx, &fy) && fx < 0.0f);

    // A tall window letterboxes the pillarboxed picture, top-left origin.
    t.resize(1000, 1001);
    SH_CHECK(t.width() == 1000 && t.height() == 1001);
    SH_CHECK(t.set_internal_resolution(640, 480));
    t.present_rect(&x, &y, &w, &h);
    SH_CHECK(w == 1000 && h == 750 && y == 1001 - 125 - 750);

    // Native: a centred 4:3 picture; display_viewport reports it while overlays draw to the window.
    gpu::Targets n(dev);
    n.view().native_render = true;
    n.resize(1920, 1080);
    n.present_rect(&x, &y, &w, &h);
    SH_CHECK(x == 240 && w == 1440 && h == 1080);
    n.set_overlay_to_window(true);
    n.display_viewport(&x, &y, &w, &h);
    SH_CHECK(x == 240 && w == 1440);
}

SH_TEST(gpu_targets_pixel_aspect) {
    FakeDevice dev;
    gpu::Targets t(dev);
    sized(t, 640, 480);
    t.set_disp(disp320x224());
    // The UI pass lands 320x224 on 4:3: (320/224) / (4/3).
    t.view().ui_pass = true;
    SH_CHECK(near(t.live_pixel_aspect(), (320.0f / 224.0f) / (4.0f / 3.0f)));
    t.view().aspect_raw = true;
    t.view().pixel_aspect = 1.25f;
    SH_CHECK(t.live_pixel_aspect() == 1.25f);
}

SH_TEST(gpu_targets_clear) {
    FakeDevice dev;
    gpu::Targets t(dev);
    sized(t, 640, 480);
    FakeCmd& cmd = dev.cmd;
    // A whole clear waits for the target's next use (a tile-based GPU begins the scene pass with it); a depth clear
    // asked for meanwhile is part of it.
    t.clear(cmd, {0, 0, 320, 224}, 255, 0, 0);
    t.clear_depth(cmd);
    SH_CHECK(cmd.passes.empty());
    SH_CHECK(t.resolved(cmd) == t.scene_color());
    SH_CHECK(cmd.passes.size() == 1 && cmd.blits.empty());
    t.flush_clear(cmd);
    SH_CHECK(cmd.passes.size() == 1);
    SH_CHECK(cmd.passes[0].color.texture == t.scene_color() && cmd.passes[0].color.clear[0] == 1.0f);
    SH_CHECK(cmd.passes[0].depthStencil.texture == t.scene_depth() &&
             cmd.passes[0].depthStencil.load == rhi::LoadOp::Clear);

    // Pillarboxed: black bars, the colour only inside the picture.
    gpu::Targets p(dev);
    sized(p, 1280, 720);
    p.view().widescreen_mode = 0;
    cmd.passes.clear();
    p.clear(cmd, {0, 0, 320, 224}, 128, 128, 128);
    SH_CHECK(cmd.passes.size() == 1 && cmd.passes[0].color.clear[0] == 0.0f && cmd.passes[0].color.clear[3] == 1.0f);
    SH_CHECK(cmd.blits.size() == 1 && cmd.blits[0].dst == p.scene_color());
    SH_CHECK(cmd.blits[0].dst_rect.x == 160 && cmd.blits[0].dst_rect.y == 0 && cmd.blits[0].dst_rect.w == 960 &&
             cmd.blits[0].dst_rect.h == 720);
    SH_CHECK(cmd.last_upload.size() == 4 && cmd.last_upload[0] == std::byte{128} &&
             cmd.last_upload[3] == std::byte{0xFF});
    // A depth clear alone keeps the colour, and waits the same way.
    cmd.passes.clear();
    p.clear_depth(cmd);
    SH_CHECK(cmd.passes.empty());
    p.begin_draw(cmd);
    SH_CHECK(cmd.passes.size() == 1 && cmd.passes[0].color.load == rhi::LoadOp::Load &&
             cmd.passes[0].depthStencil.load == rhi::LoadOp::Clear);
}

SH_TEST(gpu_targets_scene_redirect) {
    FakeDevice dev;
    gpu::Targets t(dev);
    SH_CHECK(!t.scene_redirect_covers(320, 256) && !t.scene_redirect(nullptr));
    SH_CHECK(t.set_scene_redirect({320, 256, 320, 224}));
    SH_CHECK(!t.set_scene_redirect({320, 256, 320, 224}));  // re-arm while live
    SH_CHECK(t.scene_redirect_covers(320, 256) && t.scene_redirect_covers(639, 479));
    SH_CHECK(!t.scene_redirect_covers(640, 256) && !t.scene_redirect_covers(319, 256));
    gpu::Rect r;
    SH_CHECK(t.scene_redirect(&r) && r.w == 320);
    SH_CHECK(!t.tick_scene_redirect() && !t.tick_scene_redirect() && t.tick_scene_redirect());
    SH_CHECK(!t.scene_redirect_covers(320, 256) && !t.tick_scene_redirect());
}

SH_TEST(gpu_targets_present_math_flip_and_area) {
    namespace pr = sh::present;
    const pr::PlacedRect r = pr::flip_y({10, 20, 30, 40}, 100);
    SH_CHECK(r.x == 10 && r.y == 40 && pr::flip_y(r, 100).y == 20);
    pr::AreaRect a;
    SH_CHECK(pr::display_area({0, 320, 0, 224}, 320, 224, {160, 0, 960, 720}, &a));
    SH_CHECK(near(a.x0, 160) && near(a.x1, 1120) && near(a.y0, 0) && near(a.y1, 720));
    SH_CHECK(!pr::display_area({0, 0, 0, 224}, 320, 224, {0, 0, 1, 1}, &a));
}

SH_TEST(gpu_targets_msaa) {
    FakeDevice dev;
    dev.set_max_samples(4);
    gpu::Targets t(dev);
    t.view().msaa_samples = 8;  // the device takes 4
    sized(t, 640, 480);
    SH_CHECK(t.samples() == 4);
    SH_CHECK(t.draw_color() != t.scene_color() && t.draw_depth());
    SH_CHECK(dev.desc(t.draw_color()).samples == 4 && dev.desc(t.draw_depth()).samples == 4);
    SH_CHECK(dev.desc(t.scene_color()).samples == 1 && (dev.desc(t.scene_color()).usage & rhi::kUsageSampled));
    SH_CHECK(!t.scene_depth());  // the single-sample depth is never attached

    // Draws go to the multisampled pair; a read resolves once, a second read needs nothing new.
    const gpu::DrawTarget d = t.for_split(screen_env(0, 0, 320, 224), disp320x224());
    SH_CHECK(d.color == t.draw_color() && d.depth_stencil == t.draw_depth());
    t.begin_draw(dev.cmd);
    SH_CHECK(t.resolved(dev.cmd) == t.scene_color());
    SH_CHECK(dev.cmd.resolves.size() == 1 && dev.cmd.resolves[0].first == t.draw_color());
    t.resolved(dev.cmd);
    SH_CHECK(dev.cmd.resolves.size() == 1);
    // A direct write of the resolve is not overwritten by a later resolve of older draws.
    t.begin_draw(dev.cmd);
    t.wrote_resolved();
    t.resolved(dev.cmd);
    SH_CHECK(dev.cmd.resolves.size() == 1);

    // A full clear goes to the multisampled target and counts as drawn.
    t.clear(dev.cmd, {0, 0, 320, 224}, 10, 20, 30);
    t.flush_clear(dev.cmd);
    SH_CHECK(!dev.cmd.passes.empty() && dev.cmd.passes.back().color.texture == t.draw_color());
    t.resolved(dev.cmd);
    SH_CHECK(dev.cmd.resolves.size() == 2);

    // Off again: the scene target is rebuilt single-sampled on the next resize check.
    t.view().msaa_samples = 0;
    SH_CHECK(t.needs_resize());
    sized(t, 640, 480);
    SH_CHECK(t.samples() == 1 && t.draw_color() == t.scene_color() && t.scene_depth());
    SH_CHECK(!t.needs_resize());

    // A backend without MSAA keeps one sample whatever is asked.
    FakeDevice plain;
    gpu::Targets p(plain);
    p.view().msaa_samples = 8;
    sized(p, 640, 480);
    SH_CHECK(p.samples() == 1 && p.draw_color() == p.scene_color() && !p.needs_resize());
}
