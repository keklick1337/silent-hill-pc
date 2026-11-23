#include "render/feedback/feedback_geometry.h"

#include <algorithm>
#include <cmath>

namespace sh::render::feedback {

Area areaFor(const AreaInputs& in) {
    Area r;
    const bool useUi = in.readerIsUiPass && in.uiAreaValid;
    const FRect& base = useUi ? in.uiArea : in.worldArea;
    const bool valid = useUi ? in.uiAreaValid : in.worldAreaValid;

    IRect vp = in.presentViewport;
    if (vp.w <= 0 || vp.h <= 0) vp = {0, 0, in.windowW, in.windowH};
    r.viewport = vp;
    r.rect = base;

    const float vx0 = static_cast<float>(vp.x), vy0 = static_cast<float>(vp.y);
    const float vx1 = static_cast<float>(vp.x + vp.w), vy1 = static_cast<float>(vp.y + vp.h);

    if (useUi && in.wideScale > 1.001f) {
        // Widened ortho: the picture rect is the viewport.
        r.rect = {vx0, vy0, vx1, vy1};
    } else if (!useUi && in.captureWorldWide) {
        // Scene-scratch strips span the world ortho's width; vertical stays the display buffer.
        r.rect.x0 = vx0;
        r.rect.x1 = vx1;
    }

    if (!valid || r.rect.x1 <= r.rect.x0 || r.rect.y1 <= r.rect.y0) {
        r.rect = {vx0, vy0, vx1, vy1};
        r.recorded = false;
        return r;
    }
    r.recorded = true;
    return r;
}

static int roundHalfUp(float v) { return static_cast<int>(std::floor(v + 0.5f)); }

bool areaPixelRect(const Area& area, IRect* out) {
    if (!area.recorded) return false;
    const int x0 = roundHalfUp(area.rect.x0), y0 = roundHalfUp(area.rect.y0);
    const int x1 = roundHalfUp(area.rect.x1), y1 = roundHalfUp(area.rect.y1);
    if (x1 <= x0 || y1 <= y0) return false;
    *out = {x0, y0, x1 - x0, y1 - y0};
    return true;
}

CaptureBlit captureBlit(const Area& area, int w, int h, bool exact1to1) {
    CaptureBlit b;
    const FRect& a = area.rect;
    const IRect& vp = area.viewport;
    const float vx0 = static_cast<float>(vp.x), vy0 = static_cast<float>(vp.y);
    const float vx1 = static_cast<float>(vp.x + vp.w), vy1 = static_cast<float>(vp.y + vp.h);

    float sx0 = std::max(a.x0, vx0), sy0 = std::max(a.y0, vy0);
    float sx1 = std::min(a.x1, vx1), sy1 = std::min(a.y1, vy1);

    if (exact1to1) {
        // Whole pixels on both sides: a scaled blit, even by one pixel, is a shift the loop repeats.
        const int ax0 = roundHalfUp(a.x0), ay0 = roundHalfUp(a.y0);
        const int ax1 = roundHalfUp(a.x1), ay1 = roundHalfUp(a.y1);
        const int sx0i = std::max(ax0, vp.x), sy0i = std::max(ay0, vp.y);
        const int sx1i = std::min(ax1, vp.x + vp.w), sy1i = std::min(ay1, vp.y + vp.h);
        sx0 = static_cast<float>(sx0i);
        sy0 = static_cast<float>(sy0i);
        sx1 = static_cast<float>(sx1i);
        sy1 = static_cast<float>(sy1i);
        b.dx0 = sx0i - ax0;
        b.dy0 = sy0i - ay0;
        b.dx1 = sx1i - ax0;
        b.dy1 = sy1i - ay0;
    } else {
        const float fw = static_cast<float>(w), fh = static_cast<float>(h);
        b.dx0 = static_cast<int>((sx0 - a.x0) / (a.x1 - a.x0) * fw + 0.5f);
        b.dx1 = static_cast<int>((sx1 - a.x0) / (a.x1 - a.x0) * fw + 0.5f);
        b.dy0 = static_cast<int>((sy0 - a.y0) / (a.y1 - a.y0) * fh + 0.5f);
        b.dy1 = static_cast<int>((sy1 - a.y0) / (a.y1 - a.y0) * fh + 0.5f);
    }
    b.src = {sx0, sy0, sx1, sy1};
    b.valid = sx1 > sx0 && sy1 > sy0 && b.dx1 > b.dx0 && b.dy1 > b.dy0;
    return b;
}

void DisplayBuffers::set(int x0, int y0, int x1, int y1, int w, int h) {
    buf_[0].x = x0;
    buf_[0].y = y0;
    buf_[1].x = x1;
    buf_[1].y = y1;
    // Buffer 0 must never spill into buffer 1's rows (the game's are exactly adjacent).
    const int gap = (y1 > y0) ? (y1 - y0) : h;
    if (gap > 0 && h > gap) h = gap;
    buf_[0].w = buf_[1].w = w;
    buf_[0].h = buf_[1].h = h;
    valid_ = (w > 0 && h > 0);
}

bool DisplayBuffers::noteUpload(int x, int y, int w, int h) {
    if (!valid_) return false;
    for (const IRect& b : buf_) {
        if (b.w <= 0 || b.h <= 0) continue;
        if (x < b.x + b.w && x + w > b.x && y < b.y + b.h && y + h > b.y) {
            suppress_ = 4;  // a few frames of hysteresis so intermittent uploads do not flicker the effect
            return true;
        }
    }
    return false;
}

bool DisplayBuffers::consumeSuppress() {
    if (suppress_ <= 0) return false;
    --suppress_;
    return true;
}

SampleResult selectSample(const SampleQuery& q, bool screenEnabled, bool scratchReady, const IRect& scratchRect,
                          bool screenReady, const DisplayBuffers& buffers) {
    SampleResult r;
    if (!screenEnabled || q.w <= 0 || q.h <= 0) return r;
    const int srcX = q.pageX + q.u0, srcY = q.pageY + q.v0;

    if (q.scratch) {
        const IRect& s = scratchRect;
        if (!scratchReady) return r;
        if (srcX < s.x || srcY < s.y || srcX + q.w > s.x + s.w || srcY + q.h > s.y + s.h) return r;
        r = {SampleKind::Scratch, 2, q.pageX - s.x, q.pageY - s.y, s.w, s.h};
        return r;
    }

    if (!screenReady || !buffers.valid()) return r;
    for (int i = 0; i < 2; i++) {
        const IRect& b = buffers.buffer(i);
        if (b.w <= 0 || b.h <= 0) continue;
        if (srcX < b.x || srcY < b.y || srcX + q.w > b.x + b.w || srcY + q.h > b.y + b.h) continue;
        r.kind = SampleKind::Screen;
        r.mode = (srcX - b.x == q.dstX && srcY - b.y == q.dstY) ? 1 : 2;
        r.offX = q.pageX - b.x;
        r.offY = q.pageY - b.y;
        r.natW = b.w;
        r.natH = b.h;
        return r;
    }
    return r;
}

}  // namespace sh::render::feedback
