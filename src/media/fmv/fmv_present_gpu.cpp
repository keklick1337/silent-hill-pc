// The FMV present hook on the engine renderer (fmv_present.h): the picture fills sh::gpu::Renderer's scene target
// in the frame's command list, then the renderer's present runs the post pass and the scaled blit like any frame,
// on GL and on the SDL3 GPU device alike.

#include "media/fmv/fmv_present.h"
#include "media/fmv/fmv_renderer.h"

#include "engine/log/sh_log.h"
#include "input/touch/touch_controls.h"
#include "render/gpu/device_host.h"
#include "render/gpu/frame_driver.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"
#include "render/overlay/overlay.h"

#include <cstdlib>
#include <memory>

#include "engine/platform/window.h"

namespace sh::fmv {

namespace {

struct Presenter {
    std::unique_ptr<FmvRenderer> renderer;
    rhi::Device* device = nullptr;  // the device `renderer` was made on
    int errorLogs = 0;
};

Presenter& State() {
    static Presenter p;
    return p;
}

void DrawOnRenderer(gpu::Renderer& r, const std::uint8_t* pixels, int width, int height, bool rgba) {
    Presenter& p = State();
    rhi::Device& dev = r.device();
    // The scene target's size, not the window's: in borderless and with render_scale the present stretches it.
    const int w = r.targets().width(), h = r.targets().height();
    const rhi::TextureH target = r.targets().scene_color();
    if (!target || w <= 0 || h <= 0) return;
    if (p.device != &dev) {
        p.renderer.reset();
        p.device = &dev;
    }
    if (!p.renderer) {
        auto created = FmvRenderer::create(dev, rhi::TextureFormat::RGBA8);
        if (!created) {
            if (p.errorLogs++ < 4) SH_WARN("[FMV] renderer: %s; movies play without a picture", created.error().c_str());
            return;
        }
        p.renderer = std::move(created.value());
    }
    rhi::CommandList& cmd = r.host().frame();
    auto up = p.renderer->upload(cmd, pixels, width, height, rgba, w, h);
    if (!up) {
        if (p.errorLogs++ < 4) SH_WARN("[FMV] frame: %s", up.error().c_str());
        return;
    }
    // A clear the scene target still owes would land on the movie frame later.
    r.targets().flush_clear(cmd);
    rhi::RenderPassDesc rp;
    rp.color = {target, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
    rp.name = "fmv";
    if (rhi::RenderPass* pass = cmd.beginRenderPass(rp)) {
        p.renderer->draw(*pass, true);
        p.renderer->unbind(*pass);
        cmd.endRenderPass();
        // The movie frame covers the whole picture: it is the newest image, not the multisampled world.
        r.targets().wrote_resolved();
    }
    // The touch screen's Skip button over the movie (nothing is queued without a touch screen), drawn on the window
    // at its own resolution when the frame presents.
    gpu::begin_window_overlay();
    sh::input::touch::draw_over_movie();
}

}  // namespace

void present_frame(const std::uint8_t* pixels, int width, int height, bool rgba) {
    gpu::Renderer* r = gpu::renderer();
    if (r == nullptr) return;
    gpu::follow_window();
    DrawOnRenderer(*r, pixels, width, height, rgba);
    r->present_picture();
    // Automation: SH_FMV_SHOT_FRAME=<n> saves the n-th presented movie frame (SCREENSHOT.BMP in the working
    // directory), as the console's shot cannot run while a movie holds the loop.
    static const char* s_shot = std::getenv("SH_FMV_SHOT_FRAME");
    static int s_frames = 0;
    if (s_shot != nullptr && ++s_frames == std::atoi(s_shot)) sh::platform::save_screenshot();
}

void release_presenter() {
    Presenter& p = State();
    p.renderer.reset();
    p.device = nullptr;
    p.errorLogs = 0;
}

}  // namespace sh::fmv
