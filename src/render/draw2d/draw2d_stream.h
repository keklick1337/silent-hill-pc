#pragma once

// Native backend of the 2D draw list (docs/RENDER.md section 2.2): a command becomes the triangles of the
// frame's vertex stream (rhi::PrimVertex, drawn by rhi::PrimRenderer) directly, with no GPU packet in between.
//
// The frame interleaves these commands with the rest of the picture at their place in the frame, so the
// backend draws into a target that owns the stream: the target says what the frame looks like at that point
// (draw offset, layer depth, the latched draw mode), opens a run of vertices for each shape, may adjust the
// corners of a shape before it is split into triangles, and batches runs that share their state.
//
// Vertex layout per shape, which the shaders and the batching rely on:
// - a quad is two triangles (0 1 2) and (1 2 3) of the hardware's vertex order (0 TL, 1 TR, 2 BL, 3 BR for
//   rectangles); a rectangle's corners are built as TL, BL, BR, TR before the split;
// - textured shapes carry their page as rhi::texturePageWord-style bits and their palette word, a texel box for the
//   filter's clamp, and `bright` 2 (texture modulated, 128 = unchanged); untextured shapes carry
//   rhi::kPageUntextured and `bright` 1;
// - a 1:1 rectangle whose far texture edge lands exactly on the page edge stores 255 and carries the missing
//   texel in the per-vertex offset (tcx/tcy = 2, half texels), since 256 does not fit a texel coordinate.

#include "render/draw2d/draw2d.h"
#include "render/rhi/prim_batch.h"

#include <cstdint>

namespace sh::draw2d {

// The frame at the point a command is drawn.
struct StreamFrame {
    int offset_x = 0, offset_y = 0;  // draw-area origin within the target
    float depth = 0.f;               // depth of the command's layer
    std::uint16_t page = 0;          // the draw mode's texture page as latched (sprites sample it)
    bool dither = false;             // the draw mode's dither flag
    bool mark_geometry = false;      // polygons count as world geometry for the filter gate (3D frames)
    bool precise = false;            // the stream carries precise positions: polygons repeat x/y there, w = 0
    bool size_rule = true;           // the GPU skips triangles wider than max_poly_width or taller than 511
    float max_poly_width = 1023.f;
};

enum class RunShape : std::uint8_t { Polygon, Sprite, Fill, Line };

// What a run of vertices is drawn with; consecutive runs that agree on it may share a draw.
struct StreamRun {
    RunShape shape = RunShape::Polygon;
    bool textured = false;
    bool translucent = false;
    std::uint16_t page = 0;  // the texels' page: the polygon's own, else the latched draw-mode page
    Palette palette;
    Rgb color;               // the first colour
    RectSize size = RectSize::Free;  // sprites and fills
    Point at;                        // sprites and fills: top-left corner, relative to the draw offset
    std::int16_t w = 0, h = 0;       // sprites and fills: size in pixels
    TexCoord uv;                     // sprites: first texel
};

class StreamTarget {
public:
    virtual ~StreamTarget() = default;

    virtual StreamFrame frame() const = 0;
    // A textured polygon makes its page the draw mode's page.
    virtual void latch_page(std::uint16_t page) = 0;
    // A draw-mode change: the page (9 bits, as the hardware latches it) and the dither flag.
    virtual void latch_mode(std::uint16_t page, bool dither) = 0;
    // The clip rectangle of the following commands.
    virtual void latch_area(const DrawArea& area) = 0;
    // The mask bit of the following commands.
    virtual void latch_mask(const Mask& mask) = 0;
    // Room for up to `vertices` vertices of one run; nullptr skips the run (nothing drawn).
    virtual rhi::PrimVertex* begin(const StreamRun& run, int vertices) = 0;
    // The corners of one shape of the run (4, or 3 for a triangle), before the split into triangles.
    virtual void shape(const StreamRun& run, rhi::PrimVertex* corners, int count) = 0;
    // The run is complete: `vertices` of the room were written.
    virtual void end(int vertices) = 0;
};

// Draws `command` into `target`.
void emit_stream(const Command& command, StreamTarget& target);

}  // namespace sh::draw2d
