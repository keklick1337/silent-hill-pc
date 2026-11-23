// The renderer queries and video-memory helpers on sh::gpu::renderer(), under the C names the game's drawers and the
// UI hosts call them by. Each answers the neutral value while no renderer exists (unit tests).
#include "render/gpu/render_queries.h"
#include "render/gpu/feedback_pass.h"
#include "render/gpu/renderer.h"
#include "render/gpu/render_state.h"
#include "render/gpu/targets.h"
#include "render/gpu/vram.h"

#include "engine/log/sh_log.h"

#include <cstdint>

using sh::gpu::Rect;
using sh::gpu::Renderer;
using sh::gpu::renderer;
using sh::gpu::Vram;

namespace {

Rect rect_of(int x, int y, int w, int h) {
    return Rect{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(w),
                static_cast<std::int16_t>(h)};
}

}  // namespace

extern "C" {

// The pixel aspect the renderer uses this frame (the CRT solve included): what sizes things to the visible frame.
float GR_LivePixelAspect(void) {
    Renderer* r = renderer();
    return r ? r->targets().live_pixel_aspect() : 1.0f;
}

int GR_HorPlusHalfWidths(float* out43, float* outWide) {
    Renderer* r = renderer();
    float a = 0.f, w = 0.f;
    const bool on = r && r->targets().hor_plus_half_widths(&a, &w);
    if (out43) *out43 = a;
    if (outWide) *outWide = w;
    return on ? 1 : 0;
}

float GR_Ui2dWideFactor(void) {
    Renderer* r = renderer();
    return r ? r->targets().ui_wide_factor() : 1.0f;
}

// A scene that draws its frame into a video-memory scratch rect and repaints the screen from it (armed every frame).
void GR_SetSceneFbRedirect(int x, int y, int w, int h) {
    if (Renderer* r = renderer())
        if (r->targets().set_scene_redirect(rect_of(x, y, w, h)))
            SH_LOG("[GPU] scene scratch redirect %dx%d at (%d,%d)", w, h, x, y);
}

// The game wrote video memory behind the transfer functions' back (the paper map's direct copy): send it up.
void GR_DirectUploadVRAMRegion(int x, int y, int w, int h) {
    Renderer* r = renderer();
    if (!r || x < 0 || y < 0 || w <= 0 || h <= 0) return;
    if (x + w > Vram::kWidth) w = Vram::kWidth - x;
    if (y + h > Vram::kHeight) h = Vram::kHeight - y;
    if (w > 0 && h > 0) r->vram().mark_dirty(rect_of(x, y, w, h));
}

}  // extern "C"
