// The game's frame on the engine renderer (RENDER.md section 3): creates the process's sh::gpu::Renderer, copies
// the render configuration (render_state.h) into its settings, and runs the frame around the game's draws: begin
// (clear, the frozen frame while paused), end (frame store for feedback, freeze capture, the 2D overlay, post and
// present). The host's frame (engine/platform/window.h) calls frame_begin / frame_end.
#pragma once

#include "render/gpu/backend_name.h"

#include <cstdint>
#include <string>

namespace sh::gpu {

class Renderer;

struct DriverParams {
    const char* window_name = "Silent Hill";
    int width = 640, height = 480;
    int fullscreen = 0;  // 0 windowed, 1 exclusive, 2 borderless
    int msaa = 0;
    BackendKind backend = BackendKind::Gl;
    bool vulkan_validation = false;
    int software_gl = 0;  // sh::gpu::SoftwareGl
    // Native Vulkan (RENDER.md section 2.2d): the folder of the pipeline cache on disk (empty: in memory only), the
    // game build a cache must come from, and the prewarm of the pipelines of earlier runs.
    std::string pipeline_cache_dir;
    std::string game_version;
    bool shader_prewarm = true;
    // The replacement pictures (render/textures/replacement_store.h): an exe module the renderer reads through this.
    std::uint32_t (*lookup_override)(int tpage, int clut, int* w, int* h, int* off_x, int* off_y, int* hires_w,
                                     int* hires_h) = nullptr;
};

// Creates the renderer and makes it sh::gpu::renderer(). False when no device could be made.
bool start(const DriverParams& params);
// Destroys it (the window goes with it). Safe to call twice.
void stop();

// Copies the render configuration and the game's per-frame state into the renderer. Cheap; called at every frame
// start and before every flush, because the game changes some of it between its table draws (the UI ortho pass).
void sync_state();

void frame_begin();
// End of the game's frame: frame store for feedback, freeze capture, the 2D overlay, post and present.
void frame_end();
// Queues the 2D overlay (console, debug panels, pointer) at every frame end, after the frame was stored and
// captured, so the frozen copies never contain it.
void set_overlay(void (*overlay)());
// Lays overlay::window_overlay() out against the window for the next present (frame_end does it for a game frame;
// a movie's frame, which presents on its own, calls it before it queues its controls).
void begin_window_overlay();

// The swap interval the next frames present with (applied only on a change).
void set_swap_interval(int interval);
// The window changed size or mode, or the configured render size changed: rebuild the targets.
void window_changed();
// window_changed when the back buffer is not the size the targets were made for. The back buffer is the truth, not
// the size events: a movie's loop drains the event queue, so a window manager's resize (the maximise right after
// the window opens, during the intro movies) never reached window_changed and the game stayed rendered and laid
// out for the configured shape, stretched into the window. Every frame start and every movie frame call it.
void follow_window();

// Called once per frame end, after present.
void add_frame_end_listener(void (*listener)());

}  // namespace sh::gpu
