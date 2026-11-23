// Framebuffer feedback bookkeeping: which window rect a feedback capture reads, where it lands,
// which display buffer a reader samples, and when the store stands down. Pure logic, no GL.
#pragma once

#include <array>
#include <cstdint>

namespace sh::render::feedback {

struct IRect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct FRect {  // x0,y0,x1,y1 in window pixels, GL bottom-left origin
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// Inputs of areaFor(): the ortho areas recorded by the world and UI passes plus the present viewport.
struct AreaInputs {
    FRect worldArea;
    bool worldAreaValid = false;
    FRect uiArea;
    bool uiAreaValid = false;
    IRect presentViewport;  // zero size = not recorded yet
    int windowW = 0, windowH = 0;
    bool readerIsUiPass = false;
    bool captureWorldWide = false;
    float wideScale = 1.0f;
};

struct Area {
    FRect rect;
    IRect viewport;
    bool recorded = false;  // false: rect is the viewport, no pass has recorded one yet
};

// The window rect the display buffer of the pass that redraws the capture occupies, and the
// viewport it is clipped to.
Area areaFor(const AreaInputs& in);

// The area rounded to whole pixels; false when no pass recorded one or it is empty.
bool areaPixelRect(const Area& area, IRect* out);

// Source rect (window pixels) and destination corners (capture texels) of the blit that fills a
// w x h capture from the area. exact1to1 keeps whole pixels on both sides so the loop never drifts.
struct CaptureBlit {
    FRect src;
    int dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;
    bool valid = false;
};
CaptureBlit captureBlit(const Area& area, int w, int h, bool exact1to1);

// The two PSX display buffers plus the hysteresis that makes the store stand down when the game
// uploads its own data into one of them.
class DisplayBuffers {
public:
    void set(int x0, int y0, int x1, int y1, int w, int h);
    bool valid() const { return valid_; }
    const IRect& buffer(int i) const { return buf_[i]; }

    // True when the upload overlapped a buffer (the store then stands down for a few frames).
    bool noteUpload(int x, int y, int w, int h);
    // Consumes one frame of the stand-down; true while it was active.
    bool consumeSuppress();

private:
    std::array<IRect, 2> buf_{};
    bool valid_ = false;
    int suppress_ = 0;
};

// How a feedback prim reads the capture.
enum class SampleKind : std::uint8_t { None, Scratch, Screen };
struct SampleResult {
    SampleKind kind = SampleKind::None;
    int mode = 0;  // 1: copy onto the same position (sampled by fragment coordinate), 2: through UVs
    int offX = 0, offY = 0, natW = 0, natH = 0;
};
struct SampleQuery {
    bool scratch = false;
    int pageX = 0, pageY = 0, u0 = 0, v0 = 0, dstX = 0, dstY = 0, w = 0, h = 0;
};
SampleResult selectSample(const SampleQuery& q, bool screenEnabled, bool scratchReady, const IRect& scratchRect,
                          bool screenReady, const DisplayBuffers& buffers);

}  // namespace sh::render::feedback
