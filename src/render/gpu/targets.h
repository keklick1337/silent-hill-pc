// Render targets of the frame (RENDER.md section 3): the internal-resolution scene colour and depth/stencil
// target, the offscreen redirect target for draw areas that are not the shown buffer, the picture and present rects
// (aspect, widescreen, CRT trim). The pure math is in src/render/present and src/render/offscreen.
//
// Every rect handed to the RHI (DrawTarget, picture_rect, present_rect) is top-left origin. The game-side queries
// (display_viewport, window_to_viewport, latches) publish their numbers bottom-left origin,
// because their consumers are written against those.
#pragma once

#include "render/gpu/gpu_types.h"
#include "render/rhi/rhi.h"

#include <array>
#include <cstdint>

namespace sh::gpu {

// Where a split draws and with which projection: the result of the draw area of its environment.
struct DrawTarget {
    rhi::TextureH color;
    rhi::TextureH depth_stencil;  // empty for the offscreen redirect (it has no depth)
    rhi::Viewport viewport;
    bool scissor = false;
    rhi::Rect scissor_rect;       // the full target when `scissor` is false
    std::array<float, 16> projection{};  // orthoProjection over the area

    // Offscreen redirect (a draw area that is not the shown buffer). `offscreen_begin`: this split enters it, the
    // caller starts the pass with LoadOp::Clear and `offscreen_clear`. `offscreen_end`: this split leaves an
    // offscreen area, whose picture the caller packs into video memory at `offscreen_writeback` before drawing (the
    // redirect target holds it top row first, no flip). Both may be set when the area changes between two splits.
    bool offscreen = false;
    bool offscreen_begin = false;
    bool offscreen_end = false;
    Rect offscreen_writeback;
    std::array<float, 4> offscreen_clear{0.5f, 0.5f, 0.5f, 0.0f};
};

// The picture's knobs (render::picture().hor_plus, render::picture().widescreen_mode, render::picture().world_v_scale, ...). The
// integration fills this from the game's config and per-frame state.
struct ViewSettings {
    bool hor_plus = true;           // 3D gameplay frame (render::picture().hor_plus)
    int widescreen_mode = 1;        // 0 pillarbox, 1 Hor+, 2 stretch
    bool menu_pillarbox = true;
    bool native_render = false;
    int ui2d_wide_frames = 0;       // a 2D screen asked for the whole window (render::picture().ui2d_wide_frames)
    bool ui_pass = false;           // render::frame_state().ui_ortho_pass
    bool item_take = false;         // render::frame_state().item_take
    bool cutscene = false;          // render::frame_state().cutscene
    float world_hscale = 0.76f;
    float world_vscale = 1.0f;
    float cutscene_vscale = 0.0f;
    float vcrop_anchor = 0.0f;
    bool aspect_raw = false;
    float pixel_aspect = 1.0f;      // the `par` knob (used when aspect_raw)
    float crt_trim = 1.0f;
    float render_scale = 1.0f;
    int msaa_samples = 0;           // the msaa key (render::graphics().msaa_samples); clamped to the device
    bool hdr_frame = false;         // hdr_frame: an RGBA16F scene target where the device has float targets
    int cfg_render_w = 0, cfg_render_h = 0;  // the configured render size
    int fullscreen_mode = 0;        // 2 = borderless: render at cfg_render_h lines, the width at the window's shape
};

// What the last on-screen split published for the game side (render::published().world_ortho_rect,
// render::published().world_ortho, render::published().world_disp and the area viewports).
struct ViewLatches {
    std::array<float, 4> hud_rect{-160.f, 160.f, -120.f, 120.f};          // UI pass ortho minus draw offset (L R T B)
    std::array<float, 4> world_ortho_rect{-160.f, 160.f, -120.f, 120.f};  // world pass ortho minus draw offset
    std::array<float, 4> world_ortho{0.f, 320.f, 0.f, 224.f};             // world pass ortho (L R T B)
    std::array<float, 2> world_disp{320.f, 224.f};
    bool world_ortho_valid = false;
    std::array<float, 4> area_vp{};     // display buffer under the world ortho: x0 y0 x1 y1, bottom-left origin
    bool area_vp_valid = false;
    std::array<float, 4> ui_area_vp{};  // same under the UI ortho
    bool ui_area_vp_valid = false;
    float feedback_wide_center = 160.f;
    float feedback_wide_scale = 1.f;
    std::array<int, 4> present_vp{};    // picture rect of the last on-screen split, bottom-left origin
};

class Targets {
public:
    explicit Targets(rhi::Device& device);
    ~Targets();
    Targets(const Targets&) = delete;
    Targets& operator=(const Targets&) = delete;

    ViewSettings& view() { return view_; }
    const ViewSettings& view() const { return view_; }

    // Window size changed or the preset's internal resolution changed.
    void resize(int window_w, int window_h);
    // Frame start: native render follows the display mode the game set.
    void track_native(const DispEnv& disp);
    // Recreate the scene target at w x h; false (and no target) when it cannot be made or w/h are not positive.
    bool set_internal_resolution(int w, int h);
    // render_scale or the MSAA sample count changed since the scene target was made (the options menu sets both live).
    bool needs_resize() const;
    // The scene depth is made sampled (the atmosphere pass reads it); a change remakes the target.
    void set_sampled_depth(bool on) { sampled_depth_ = on; }
    // TAAU and the spatial upscale (temporal_pass.h): while below 1, on-screen draws go into the top-left `scale`
    // of their viewport and scissor; world_scaled() then gives the last such draw's full and scaled viewports.
    void set_world_scale(float scale);
    bool world_scaled(rhi::Viewport* full, rhi::Viewport* scaled) const;
    void destroy();

    int width() const { return width_; }  // scene target size (render::published().scene_width / render::published().scene_height)
    int height() const { return height_; }
    int present_width() const { return present_w_; }
    int present_height() const { return present_h_; }

    // The display environment the queries below measure against (activeDispEnv); for_split also sets it.
    void set_disp(const DispEnv& disp) { disp_ = disp; }
    const DispEnv& disp() const { return disp_; }

    // The target, viewport, scissor and projection a split recorded under `env` / `disp` draws with. On-screen
    // splits also update latches().
    DrawTarget for_split(const DrawEnv& env, const DispEnv& disp);
    const DrawTarget& last() const { return last_; }  // what the last for_split returned
    const ViewLatches& latches() const { return latches_; }

    // MSAA (RENDER.md section 7.8): with msaa above 0 the scene draws into a multisampled colour + depth pair and
    // scene_color() is its single-sample resolve, the texture everything else reads. Draws go to draw_color() /
    // draw_depth() after begin_draw(); a read of scene_color() goes through resolved(); a direct write to it (a blit)
    // calls wrote_resolved(), and the next begin_draw() copies it back into the multisampled target.
    rhi::TextureH scene_color() const { return scene_color_; }
    // RGBA16F with hdr_frame on a device with float targets (render/post/hdr.h), else RGBA8.
    rhi::TextureFormat scene_format() const { return scene_format_; }
    rhi::TextureH scene_depth() const { return scene_depth_; }
    rhi::TextureH draw_color() const { return samples_ > 1 ? ms_color_ : scene_color_; }
    rhi::TextureH draw_depth() const { return samples_ > 1 ? ms_depth_ : scene_depth_; }
    int samples() const { return samples_; }
    void begin_draw(rhi::CommandList& cmd);
    rhi::TextureH resolved(rhi::CommandList& cmd);
    void wrote_resolved();
    rhi::TextureH offscreen_color() const { return offscreen_color_; }
    bool offscreen_active() const { return offscreen_active_; }
    const Rect& offscreen_area() const { return offscreen_area_; }

    // Clear of a video-memory rect of the shown buffer mapped onto the scene target. It
    // clears the whole scene target whatever the rect: black bars outside the picture rect, the colour inside;
    // depth to 1 and stencil to 0 (a load-op clear cannot keep stencil; a PS1 fill leaves no mask bit either).
    void clear(rhi::CommandList& cmd, const Rect& vram_rect, std::uint8_t r, std::uint8_t g, std::uint8_t b);
    // Depth to 1 and stencil to 0, colour kept: each ordering table tests depth only against itself, and the item
    // bracket starts on an empty depth buffer.
    void clear_depth(rhi::CommandList& cmd);
    // A clear of the whole target waits (clear) until the target is next drawn into or read: begin_draw, resolved,
    // clear_depth and a direct user of scene_color() (the movie's present) call this first.
    void flush_clear(rhi::CommandList& cmd);

    // Picture rect inside the scene target, top-left origin.
    void picture_rect(int* x, int* y, int* w, int* h) const;
    // Window-space present rect of the picture (letterbox / pillarbox), top-left origin.
    void present_rect(int* x, int* y, int* w, int* h) const;
    // The picture rect, or the present rect while overlays draw to the window.
    void display_viewport(int* x, int* y, int* w, int* h) const;
    // Window pixels -> fraction of the picture; false when in the bars.
    bool window_to_viewport(int mx, int my, float* fx, float* fy) const;
    // Overlays drawn onto the window over the scaled native picture.
    void set_overlay_to_window(bool on) { overlay_to_window_ = on; }

    float live_pixel_aspect() const;  // PsxDisplayPixelAspect / GR_LivePixelAspect
    float ui_wide_factor() const;     // GR_Ui2dWideFactor
    bool ui2d_wide() const;
    // GR_HorPlusHalfWidths: authored 4:3 and installed half-widths of the world ortho; false = no widening.
    bool hor_plus_half_widths(float* authored, float* installed) const;

    // Scene scratch redirect (GR_SetSceneFbRedirect): a scene that draws its frame into a
    // video-memory scratch rect and repaints the screen from it. Armed by the game every frame it submits the area;
    // live for a few presents after. set_scene_redirect returns true when it arms after a lapse (for the log line),
    // tick_scene_redirect (once per present) true when it just lapsed.
    bool set_scene_redirect(const Rect& rect);
    bool scene_redirect_covers(int x, int y) const;
    bool scene_redirect(Rect* rect) const;  // false when not live
    bool tick_scene_redirect();

private:
    void clear_pass(rhi::CommandList& cmd, const std::array<float, 4>& colour);
    void depth_clear_pass(rhi::CommandList& cmd);
    void release_scene();
    void release_seed();
    int wanted_samples() const;
    rhi::TextureFormat wanted_format() const;
    void make_multisampled(int samples);
    bool seed_ready();
    void seed(rhi::CommandList& cmd);
    bool ensure_offscreen(int w, int h);
    struct Size { int w, h; };
    Size scaled(int w, int h) const;

    rhi::Device& device_;
    rhi::TextureH scene_color_, scene_depth_;
    rhi::TextureFormat scene_format_ = rhi::TextureFormat::RGBA8;
    bool hdr_failed_ = false;
    rhi::TextureH ms_color_, ms_depth_;
    int samples_ = 1;
    bool sampled_depth_ = false;
    float world_scale_ = 1.0f;
    bool world_scaled_ = false;
    rhi::Viewport world_full_{}, world_small_{};
    bool made_sampled_depth_ = false;
    int ms_failed_ = 0;          // the sample count the device refused at this size
    int logged_samples_ = -1;
    bool ms_dirty_ = false;      // the multisampled target has draws its resolve lacks
    bool ms_stale_ = false;      // the resolve was written directly; the multisampled target lacks it
    rhi::ShaderH seed_vs_, seed_fs_;
    rhi::PipelineH seed_pipe_;
    rhi::BufferH seed_triangle_;
    rhi::SamplerH seed_sampler_;
    bool seed_failed_ = false;
    rhi::TextureH offscreen_color_;
    int offscreen_w_ = 0, offscreen_h_ = 0;
    rhi::TextureH solid_;  // 1x1 fill source of the pillarbox clear
    bool clear_pending_ = false;  // flush_clear: a whole clear, or only the depth
    bool depth_clear_pending_ = false;
    std::array<float, 4> pending_colour_{};
    int width_ = 0, height_ = 0;
    int present_w_ = 0, present_h_ = 0;
    float applied_scale_ = 1.0f;  // view_.render_scale the scene target was last sized with
    ViewSettings view_;
    DispEnv disp_;
    bool overlay_to_window_ = false;
    bool offscreen_active_ = false;
    Rect offscreen_area_;
    DrawTarget last_;
    ViewLatches latches_;
    Rect redirect_;
    int redirect_ttl_ = 0;
};

}  // namespace sh::gpu
