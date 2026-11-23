// The game's display on the primitive renderer (docs/RENDER.md section 3): the two display buffers and
// their swap, the draws of the engine frame's layers for the buffer being built, a 2D command drawn at once, the
// video-memory transfers and the draw sync. Everything does nothing while sh::gpu::renderer() is null (unit tests).
//
// Behaviour spec (read, not copied): the PsyQ libgs frame calls (GsInitGraph, GsDefDispBuff, GsSwapDispBuff,
// GsSortClear, GsDrawOt) and the libgpu environment, transfer and sync calls as the PS1 game used them.
#pragma once

#include <cstdint>

#include "render/draw2d/draw2d.h"
#include "render/frame/frame.h"
#include "render/gpu/gpu_types.h"
#include "render/gpu/split_builder.h"

namespace sh::gpu::display {

// The split builder everything the game's frame draws goes through, flushed into renderer().
SplitBuilder& builder();

// The host frame loop around the draws: begin_scene returns true when it opened a new frame (whatever an earlier
// frame left in the builder is then dropped). Installed by the host (game_display_host.cpp).
struct FrameHooks {
    bool (*begin_scene)() = nullptr;
    void (*end_scene)() = nullptr;
};
FrameHooks& frame_hooks();

// ---- Display buffers ----

// A new display mode of `width` x `height`: the renderer is reset (drawing enabled, the draw area cleared, the open
// frame dropped) and both buffers get the engine's environments (origin at the video-memory corner, the draw offset
// at the screen centre, the background cleared each frame).
void init(int width, int height);
// Where the PS1 display pages were (the motion blur and the dream overlays sample them as textures).
void set_display_pages(int x0, int y0, int x1, int y1);
// Flips to the other buffer and puts its environments; the clear colour follows the game's copy.
void swap_buffers();
// The layers drawn next are the last presented frame's, drawn again (a frame between two game updates): the
// frame's depth maximum is the one they were first drawn with.
void set_presenting_again(bool again);
// The buffer being built (0 or 1).
int active_buffer();
// Clears the background of the buffer being built to (r, g, b) from now on.
void sort_clear(std::uint8_t r, std::uint8_t g, std::uint8_t b);
// false: nothing is drawn until it is enabled again.
void set_display_enabled(bool enabled);

// ---- Layers of the frame being built (sh::frame::build_buffer()) ----

// Drops what the layer holds for the buffer being built (a table clear).
void clear_layer(sh::frame::Layer layer);
// Draws and consumes the layer, slots from the top down, depth-tested against itself (a table draw).
void draw_layer(sh::frame::Layer layer);
// Before the world layer is drawn: the draw-area changes that send the scene offscreen and back.
void prepare_world_layer();
// Draws one 2D command now, outside the frame's layers, at the depth the last layer draw ended on.
void draw_now(const sh::draw2d::Command& command);
// How a world slot maps to view Z in the whole-town view (slot b covers view Z ~ b << shift; 0 = none).
void set_view_z_shift(int shift);
// The isolated inventory item pass: drawn on an empty depth buffer, with its own depth rules, until it ends.
void item_depth_begin();
void item_depth_end();

// ---- Video memory and sync ----

// Flushes what was drawn so far (the transfers below see it).
void sync();
// A clear of the rect is a clear of what is drawn there too.
void clear_image(const Rect& rect, std::uint8_t r, std::uint8_t g, std::uint8_t b);

}  // namespace sh::gpu::display
