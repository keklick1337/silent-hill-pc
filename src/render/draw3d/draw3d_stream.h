#pragma once

// Native backend of the world-space effect list: a polygon becomes the triangles of the frame's vertex stream
// (rhi::PrimVertex, drawn by rhi::PrimRenderer) with no GPU packet in between, every per-corner value taken from
// the command itself.
//
// The frame interleaves these polygons with the rest of the 3D picture at their place, so the backend draws
// into a target that owns the stream (as draw2d_stream.h does): the target says what the frame looks like at that point,
// opens the polygon's run, turns the polygon's depth into the stream's depth, and emits the finished triangles (the
// renderer's near-plane clip and size rule run there).
//
// Vertex layout, which the shaders and the batching rely on (the same as the 2D backend's polygons):
// - a quad is two triangles (0 1 2) and (1 2 3) of the hardware's vertex order;
// - x/y are the game's screen position plus the draw offset; ppx/ppy/ppw the precise position (ppw 0 = affine);
// - vsx/vsy/vsz the view-space position, ny = 1 when it is known, nx = 1 for a corner that casts no shadow,
//   nz = the character fade;
// - textured polygons carry their page in rhi::texturePageWord-style bits, the palette word, a texel box for the filter's
//   clamp (one texel short on the far edges) and `bright` 2; untextured ones rhi::kPageUntextured and `bright` 1.

#include "render/draw3d/draw3d.h"
#include "render/rhi/prim_batch.h"

#include <array>
#include <cstdint>

namespace sh::draw3d {

// The frame at the point a polygon is drawn.
struct StreamFrame {
    float offset_x = 0.f, offset_y = 0.f;  // draw-area origin within the target
    float depth = 0.f;                     // depth of the polygon's layer
    std::uint16_t page = 0;                // the draw mode's texture page as latched
    bool dither = false;                   // the draw mode's dither flag
    bool mark_geometry = false;            // polygons count as world geometry for the filter gate (3D frames)
    bool precise = false;                  // the stream carries precise positions
    bool view_space = false;               // the stream carries view-space positions
    float edge_max = 8192.f;               // a precise corner farther off screen than this is drawn affine
    float view_space_reach = 16384.f;      // view space is not needed when every corner is farther than this
    bool near_clip = false;                // polygons crossing the near plane are clipped (by the target)
    float near_z = 16.f;                   // view depth of the near plane
};

// The polygon's flags as the renderer applies them (the target may not honour a flag in every mode).
struct PrimFlags {
    bool affine = false;
    bool no_flashlight = false;
};

// What a polygon's run is drawn with.
struct StreamRun {
    std::uint8_t count = 4;
    bool textured = false;
    bool gouraud = false;
    bool translucent = false;
    std::uint16_t page = 0;
    Palette palette;
    Rgb color;  // the first colour
    LightKind light = LightKind::None;
    std::array<Normal, 4> normals{};  // the polygon's corners in the hardware's order (0 1 2 3)
};

class StreamTarget {
public:
    virtual ~StreamTarget() = default;

    // First call for every polygon: the flags as they take effect.
    virtual PrimFlags begin_prim(const Polygon& polygon) = 0;
    // A textured polygon that draws nothing still makes its page the draw mode's page.
    virtual void latch_page(std::uint16_t page) = 0;
    // Room for up to `vertices` vertices of the polygon's run (a textured run latches its page); nullptr skips it.
    virtual rhi::PrimVertex* begin(const StreamRun& run, int vertices) = 0;
    // Read after begin(): the frame the polygon is drawn into.
    virtual StreamFrame frame() const = 0;
    // A corner's precise position was set (`resolved`: from the command; else it repeats x/y with ppw 0).
    virtual void precise(rhi::PrimVertex& corner, bool resolved) = 0;
    // A corner's view-space position was set; the projection it was transformed with.
    virtual void view_projection(float ofx, float ofy, float h) = 0;
    // The polygon's corners (hardware order of a triangle, or 0 1 3 2 of a quad) take their depth; returns the alpha
    // the colours carry.
    virtual std::uint8_t depth(rhi::PrimVertex* corners, int count, const Polygon& polygon) = 0;
    // The polygon's triangles (3, or 6 for a quad: corners 0 1 2 and 1 2 3) are complete: the target emits them.
    virtual void end(const StreamRun& run, rhi::PrimVertex* triangles, int vertices) = 0;
};

// Draws `command` into `target`.
void emit_stream(const Command& command, StreamTarget& target);

}  // namespace sh::draw3d
