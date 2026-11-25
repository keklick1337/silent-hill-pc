// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/app/shader_prewarm_screen.h"

#include "engine/app/screen_kit/painter.h"
#include "engine/app/screen_kit/text_raster.h"
#include "engine/log/sh_log.h"
#include "engine/platform/window.h"
#include "i18n/localization.h"
#include "render/gpu/device_host.h"
#include "render/gpu/frame_driver.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"
#include "render/overlay/overlay.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <filesystem>
#include <string>

extern "C" const char* PcPort_GetGameDataPath(void);

namespace sh::app {

namespace {

namespace kit = sh::screen_kit;

// The line of text as an overlay texture, made again when the text or its size changes.
class Line {
public:
    ~Line() {
        if (texture_) overlay::window_overlay().textures().destroy(texture_);
    }
    void set(kit::TextRaster& raster, const std::string& text, float px, const kit::TextFace& face) {
        if (texture_ && text == text_ && px == px_) return;
        text_ = text;
        px_ = px;
        overlay::TextureStore& store = overlay::window_overlay().textures();
        if (texture_) store.destroy(texture_);
        texture_ = 0;
        const kit::TextBitmap bitmap = raster.render(text, px, face.families, face.rtl);
        if (bitmap.width <= 0 || bitmap.height <= 0) return;
        width_ = bitmap.width;
        height_ = bitmap.height;
        texture_ =
            store.create(width_, height_, overlay::Format::Alpha8, overlay::Filter::Nearest, bitmap.alpha.data());
    }
    void draw(float center_x, float top, const overlay::Color& color) const {
        if (!texture_) return;
        overlay::window_overlay().quad(
            overlay::Space::Target,
            {center_x - static_cast<float>(width_) / 2.f, top, static_cast<float>(width_), static_cast<float>(height_)},
            color, texture_);
    }
    float height() const { return static_cast<float>(height_); }

private:
    overlay::TextureId texture_ = 0;
    std::string text_;
    float px_ = 0.f;
    int width_ = 0, height_ = 0;
};

std::string progress_text(unsigned percent) {
    std::string text = sh::i18n::ui_text("ui.gpu.preparing_shaders");
    const std::string placeholder = "{percent}";
    if (const std::size_t at = text.find(placeholder); at != std::string::npos)
        text.replace(at, placeholder.size(), std::to_string(percent));
    return text;
}

// A black picture with the line and a bar under it, presented like a movie frame.
void draw_frame(gpu::Renderer& r, kit::TextRaster& raster, Line& line, const kit::TextFace& face, float fraction) {
    gpu::follow_window();
    rhi::CommandList& cmd = r.host().frame();
    const rhi::TextureH target = r.targets().scene_color();
    if (target) {
        r.targets().flush_clear(cmd);
        rhi::RenderPassDesc rp;
        rp.color = {target, rhi::LoadOp::Clear, rhi::StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
        rp.name = "fmv";
        if (cmd.beginRenderPass(rp)) cmd.endRenderPass();
        r.targets().wrote_resolved();
    }
    gpu::begin_window_overlay();
    overlay::Overlay& o = overlay::window_overlay();
    const overlay::Frame& frame = o.frame();
    if (frame.valid()) {
        const float w = static_cast<float>(frame.target_w), h = static_cast<float>(frame.target_h);
        const float px = std::max(14.f, std::min(w, h * 16.f / 9.f) / 40.f);
        const unsigned percent = static_cast<unsigned>(std::clamp(fraction, 0.f, 1.f) * 100.f);
        line.set(raster, progress_text(percent), px, face);
        const float top = h * 0.5f - line.height();
        line.draw(w * 0.5f, top, {0.91f, 0.91f, 0.91f, 1.f});
        // The colours of the menus' underline (screen_kit::Style): grey track, gold fill.
        const float bar_w = std::min(w * 0.6f, px * 22.f), bar_h = std::max(2.f, px * 0.18f);
        const float x0 = (w - bar_w) * 0.5f, y0 = top + line.height() + px * 0.6f;
        o.rect(overlay::Space::Target, {x0, y0, bar_w, bar_h}, {0.25f, 0.25f, 0.25f, 1.f});
        float filled = bar_w * std::clamp(fraction, 0.f, 1.f);
        // A right-to-left language fills the bar from the right.
        const float fill_x = face.rtl ? x0 + bar_w - filled : x0;
        o.rect(overlay::Space::Target, {fill_x, y0, filled, bar_h}, {160.f / 255.f, 128.f / 255.f, 64.f / 255.f, 1.f});
    }
    r.present_picture();
}

}  // namespace

void prepare_shaders() {
    gpu::Renderer* r = gpu::renderer();
    if (r == nullptr) return;
    rhi::Device& device = r->device();
    rhi::PipelineWarmup w = device.pipelineWarmup();
    if (!w.fresh || w.done >= w.total) return;
    SH_LOG_INFO(RENDER, "preparing shaders: %u pipelines to build (the pipeline cache was rebuilt)", w.total);
    const Uint64 start = SDL_GetTicks();
    kit::TextRaster raster(std::filesystem::path(PcPort_GetGameDataPath()) / "font");
    const sh::i18n::Language& language = sh::i18n::language();
    const kit::TextFace face = kit::face_for(language.fonts, language.direction == sh::i18n::Direction::Rtl);
    Line line;
    while (w.done < w.total) {
        sh::platform::pump_events();
        draw_frame(*r, raster, line, face, static_cast<float>(w.done) / static_cast<float>(std::max(w.total, 1u)));
        // The present waits for the display with vsync; without it the loop must not spin.
        SDL_Delay(16);
        w = device.pipelineWarmup();
    }
    draw_frame(*r, raster, line, face, 1.f);
    SH_LOG_INFO(RENDER, "preparing shaders: done in %llu ms", static_cast<unsigned long long>(SDL_GetTicks() - start));
}

}  // namespace sh::app
