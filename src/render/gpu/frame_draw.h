// Engine Frame consumer of the primitive renderer (RENDER.md sections 4 and 3): draws one sh::frame::Entry
// into the SplitBuilder at its place in the frame. Draw2D and Draw3D commands become vertices through the commands'
// own PSX backends (sh::draw2d::emit_stream, sh::draw3d::emit_stream) with a target that keeps the split batching, page
// resolution, depth channel and widescreen rules; Scene, Mesh and Capture entries become their splits.
#pragma once

#include "render/frame/frame.h"
#include "render/gpu/split_builder.h"

#include <cstdint>

namespace sh::gpu {

// The ortho of the pass being drawn, for the widescreen rules of feedback strips and full-frame fills.
struct WideRules {
    bool ui_ortho_pass = false;
    bool world_ortho_valid = false;
    float world_ortho[4] = {0.f, 320.f, 0.f, 224.f};  // left, right, top, bottom
    float world_disp[2] = {320.f, 224.f};
    float feedback_wide_scale = 1.f;
    float feedback_wide_center = 160.f;
};

// A sprite reading a display buffer or the scene-scratch rect samples the render-resolution capture instead: the
// texture registry id (0 none), the sampling mode and the override footprint.
using FeedbackSampleFn = std::uint32_t (*)(bool scratch, int page_x, int page_y, int u0, int v0, int dst_x,
                                           int dst_y, int w, int h, int* mode, int* off_x, int* off_y, int* nat_w,
                                           int* nat_h);

// What the frame looks like where the entry is drawn (filled by whoever walks the frame).
struct FrameDrawContext {
    float layer_depth = 0.f;      // NDC depth seed of the layer (the OT bucket being walked)
    bool frame_3d = false;        // the frame draws the 3D world
    bool force_item_depth = false;  // isolated inventory item pass
    bool ui2d_layer = false;      // the layer holds scaled 2D text
    bool pgxp = false;            // the stream carries precise positions
    bool poly_size_cull = true;   // the PS1 polygon size rule
    bool whole_map_far = false;   // whole-town view: no size rule
    float poly_size_wide_scale = 1.f;
    bool view_space = false;      // the frame needs view-space positions (flashlight, shadows)
    bool world_depth = true;      // world-mesh polygons get the painter depth modes (render::precise().world_depth)
    bool pp_lit = false;          // the per-pixel lighting side channel is active
    bool dream_feedback = true;   // false drops the scene-scratch composite
    WideRules wide;

    // Depth mode of a 2D command's split from the depth records keyed by its packet (null: none).
    DepthMode (*split_depth)(const void* key) = nullptr;
    // A draw area moved to offscreen video memory big enough to be a scene capture (GR_SetSceneFbRedirect).
    void (*scene_redirect)(int x, int y, int w, int h) = nullptr;
    FeedbackSampleFn feedback_sample = nullptr;
};

void draw_frame_entry(SplitBuilder& out, const sh::frame::Entry& entry, const FrameDrawContext& ctx);

// NDC depth seed of a layer's slot, `walked` slots down from the top of a layer of `slots` (0 the farthest; past the
// bottom it stays at the nearest). With a view-Z shift registered for the layer
// (`view_z_shift` > 0: slot b covers view Z ~ b << shift) the seed is on the linear view-Z scale.
float slot_depth(int walked, int slots, int view_z_shift);

// Before the world layer of `buffer` is drawn: an area past the display buffers is a scene drawn into video memory,
// announced to `scene_redirect` before anything of the layer is drawn; the next on-screen area ends it and becomes
// the capture of that rect into video memory (and the layer's depth steps down one slot there).
void resolve_scene_captures(sh::frame::Frame& frame, int buffer, void (*scene_redirect)(int, int, int, int));

// ---- Shared rules ----

// Republishes the draw offset of the environment into sh::frame::state() (PublishDrawEnv).
void publish_draw_env(const PrimState& state);
// Draw-area change: corner and far corner (exclusive), 10 bits each. Routes an area past the display buffers
// offscreen (a small scratch tile) or to the scene capture (a big one, `scene_redirect`), then republishes the offset.
void latch_draw_area(SplitBuilder& out, int x0, int y0, int x1, int y1, void (*scene_redirect)(int, int, int, int));

// Rect corners (4: top-left, bottom-left, bottom-right, top-right): the scene-scratch soft focus and the UI-pass
// feedback strips stretched across a widened ortho.
void rect_wide_stretch(Vertex* corners, const PrimState& state, const WideRules& wide);
// A flat fill covering the whole PS1 frame maps onto the world ortho's extent.
void stretch_full_frame_fill(Vertex* corners, const WideRules& wide);
// dream_blur_strength on the blending soft-focus layers of the scene scratch.
void scratch_damp(Vertex* corners, const PrimState& state);
// Points the state's override at the feedback capture for a free-size sprite at (x, y) (relative to the draw
// offset) reading texels from (u, v); nothing when the hook finds no capture.
void sample_feedback(PrimState& state, FeedbackSampleFn sample, int x, int y, int u, int v, int w, int h);

}  // namespace sh::gpu
