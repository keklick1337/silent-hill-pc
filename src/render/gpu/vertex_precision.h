// Vertex precision of 3D primitives (RENDER.md section 3): pure functions over the vertices of one polygon
// that every 3D producer shares: per-vertex depth from the GTE SZ or
// PGXP W, welding of shared corners, near-plane clipping in view space, the PS1 polygon size rule, the per-pixel
// lighting normal side channel, and the per-frame SZ normaliser. Nothing here is keyed by a packet address.
#pragma once

#include "render/gpu/gpu_types.h"

#include <cstdint>
#include <span>

namespace sh::draw3d {
struct Polygon;
}

namespace sh::gpu::precision {

enum class DepthKind : std::uint8_t { None, Flat, Exact, Actor };

struct DepthIn {
    bool found = false;
    std::uint32_t sz[4] = {0, 0, 0, 0};  // a triangle uses slots 1..3 (the FIFO's SZ1..SZ3), a quad 0..3
    DepthKind kind = DepthKind::None;
    bool far = false;
    std::uint8_t alpha = 255;
    bool semi = false;
    bool last_bucket = false;  // the polygon sits in the OT's last bucket (whole-map probe statistics only)
};

// Per-frame inputs; the frame driver refreshes them from the render state (render_state.h) each frame.
struct Settings {
    bool pgxp = false;
    bool force_item_depth = false;
    bool near_clip = true;
    float near_z = 16.f;
    float edge_max = 8192.f;
    bool poly_size_cull = true;
    float poly_size_wide_scale = 1.f;
    bool whole_map_far = false;
    int debug = 0;  // render::PreciseVertices::debug bits
    int whole_map_probe = 0;
    bool actor_depth = true;
    bool world_depth = true;
    int world_far_bias = 64;
    float weld_px = 0.f;  // 0 = weld off (the shipped behaviour)
    float weld_w_ratio = 1.04f;
    // Projection of the newest vertex: the near clipper's fallback when no polygon projection was latched.
    float screen_offset_x = 0.f, screen_offset_y = 0.f, projection_distance = 1.f;
    bool pixel_lit_active = false;
};
Settings& settings();

// Diagnostic counters of the [PGXP] coverage log lines. The owner prints and clears them.
struct Counters {
    unsigned precise = 0, affine = 0, clipped = 0, oversize = 0;
    unsigned apply_calls = 0, parse_miss = 0, parse_hit = 0, hit_flat = 0, hit_exact = 0, hit_none = 0;
    unsigned item_non_exact = 0;
    unsigned wm_precise_prims = 0, wm_deviating_prims = 0;
    unsigned wm_last_prims = 0, wm_last_affine = 0, wm_last_hit = 0;
    unsigned wm_parsed = 0, wm_precise_verts = 0, wm_affine_verts = 0;
    float wm_w_min = 0.f, wm_w_max = 0.f;
};
Counters& counters();

// Constant linear view-depth to NDC scale of the PGXP depth channel (2^18 SZ units).
inline constexpr float kPgxpDepthSzMax = 262144.0f;
float ndc_from_view_z(float vz);

// New frame: bump_generation() (weld table) and roll_sz_max().
void begin_frame();
void bump_generation();
// Rolls the SZ normaliser: the largest SZ noted since the last roll becomes the reference.
void roll_sz_max();
void note_sz(std::uint32_t sz);
std::uint32_t sz_max_prev();  // raw reference (g_szMaxPrevFrame)
float sz_max();               // the scene's depth scale: the constant 2^18
float item_sz_max();          // the reference floored at 1 (the isolated item's depth scale)

// Per-polygon state: the draw-environment offset the corners were built under and whether the polygon is forced
// affine.
void begin_polygon(bool affine, float draw_ofs_x, float draw_ofs_y);
// The view projection (offset and focal length) of the polygon being built, for the near clipper's reprojection.
// Once latched it stays the fallback for later polygons until latched again.
void set_polygon_projection(float ofs_x, float ofs_y, float h);

// Writes vertex depth (z, depth mark) for the corners of one polygon; returns the polygon's alpha (255 = opaque).
std::uint8_t apply_depth(Vertex* corners, const DepthIn& in, bool quad);

// Snaps a precise corner onto an earlier one within the weld tolerance so shared edges have no cracks.
void weld(float* x, float* y, float* w);

bool near_clip_eligible(const Vertex* triangles, int count);
// One triangle exceeds the PS1 rasterizer's 1023 x 511 box.
bool oversized(const Vertex* triangle);
// Near-clips the triangle list in place (room for 12 vertices must exist at `triangles`), then applies the PS1
// size rule to integer-path polygons. `room` = false when the stream has no space for the clip's growth: the
// polygon is then forced affine instead. Returns how many vertices remain.
int emit_polygon(Vertex* triangles, int count, bool room = true);

// Per-pixel lighting side channel: one view-space normal per vertex of the frame's stream, parallel to it.
void lit_fill(std::span<const float> corner_normals, int kind, std::uint32_t first, int emitted, const int* corner_map);
void lit_zero_to(std::uint32_t end);
const float* lit_finish(std::uint32_t count);

// What the end of a draw3d run needs for the side channel (Draw3DTarget::end).
struct LitRun {
    bool textured = false;
    bool gouraud = false;
    int light = 0;           // 0 none, 1 world, 2 character
    float normals[12] = {};  // corners 0..3, xyz each
};
// Emits the run's triangles (emit_polygon) whose stream index is `first` and feeds the side channel; returns the
// emitted vertex count.
int end_polygon(Vertex* triangles, int vertices, std::uint32_t first, const LitRun& run, bool room = true);

}  // namespace sh::gpu::precision
