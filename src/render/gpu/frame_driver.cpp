// The game's frame on the engine renderer (frame_driver.h).
#include "render/gpu/frame_driver.h"

#include "engine/log/sh_log.h"
#include "render/frame/frame.h"
#include "render/gpu/atmosphere_pass.h"
#include "render/gpu/device_host.h"
#include "render/gpu/feedback_pass.h"
#include "render/gpu/page_resolve.h"
#include "render/gpu/post_pass.h"
#include "render/gpu/render_queries.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "render/gpu/shadow_pass.h"
#include "render/gpu/split_renderer.h"
#include "render/gpu/targets.h"
#include "render/gpu/textures.h"
#include "render/gpu/vertex_precision.h"
#include "render/overlay/overlay.h"
#include "render/post/hdr.h"
#include "render/present/present_math.h"
#include "render/scene/scene_c.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>


namespace sh::gpu {

namespace {

std::unique_ptr<Renderer> g_renderer;
std::vector<void (*)()> g_frame_end_listeners;
bool g_overlay_native = false;  // this frame's overlay goes onto the window after the present
void (*g_overlay_hook)() = nullptr;

void fill_view(ViewSettings& v) {
    const render::PictureShape& pic = render::picture();
    const render::FrameState& frame = render::frame_state();
    const render::GraphicsSettings& gfx = render::graphics();
    v.hor_plus = pic.hor_plus != 0;
    v.widescreen_mode = pic.widescreen_mode;
    v.menu_pillarbox = pic.menu_pillarbox != 0;
    v.native_render = pic.native_render != 0;
    v.ui2d_wide_frames = pic.ui2d_wide_frames;
    v.ui_pass = frame.ui_ortho_pass != 0;
    v.item_take = frame.item_take != 0;
    v.cutscene = frame.cutscene != 0;
    v.world_hscale = pic.world_h_scale;
    v.world_vscale = pic.world_v_scale;
    v.cutscene_vscale = pic.cutscene_v_scale;
    v.vcrop_anchor = pic.world_v_crop_anchor;
    v.aspect_raw = pic.aspect_raw != 0;
    v.pixel_aspect = pic.pixel_aspect;
    v.crt_trim = pic.crt_aspect_trim;
    v.render_scale = pic.render_scale;
    v.msaa_samples = gfx.msaa_samples;
    v.hdr_frame = post::hdr().frame;
    v.cfg_render_w = gfx.render_width;
    v.cfg_render_h = gfx.render_height;
    v.fullscreen_mode = gfx.fullscreen_mode;
}

void fill_shade(ShadeInputs& s, Renderer& r) {
    const render::PreciseVertices& precise = render::precise();
    const render::GraphicsSettings& gfx = render::graphics();
    const render::Fog& fog = render::fog();
    const render::Flashlight& light = render::flashlight();
    s.pgxp = precise.enabled != 0;
    s.sz_max = precision::sz_max();
    s.far_w_clamp = precise.far_w_clamp;
    s.world_far_bias = static_cast<float>(precise.world_far_bias);
    s.pgxp_zbuffer = gfx.depth_buffer != 0;

    // The fog volume reproduces soft_fog's ramp, so the surfaces it lies over are fogged by that ramp too.
    s.soft_fog = fog.soft != 0 || r.atmosphere().volumetric();
    s.fog_color = {fog.color[0], fog.color[1], fog.color[2]};
    s.fog_strength = fog.strength;

    s.native_render = render::picture().native_render != 0;
    s.dither_suppressed = render::frame_state().dither_suppressed != 0;
    s.texture_filter = gfx.texture_filter;
    s.aniso_level = gfx.aniso_level;
    s.menu_filter = gfx.menu_filter != 0;
    s.dither = gfx.dither != 0;
    s.deband = gfx.deband != 0;
    s.window_width = render::published().scene_width;
    s.foliage_edge = gfx.foliage_aa;
    s.multisampled = r.targets().samples() > 1;

    s.per_pixel_flashlight = light.per_pixel != 0;
    s.flashlight_active = light.active != 0;
    s.flashlight_fps_mode = light.fps_mode != 0;
    s.flashlight_style = light.style != 0;
    // A tonemapper of the HDR frame takes the beam's highlights over from the shader's own roll-off.
    s.soft_highlights = post::sceneRollsOffHighlights(gfx.soft_highlights != 0, post::hdr(),
                                                      r.targets().scene_format() == rhi::TextureFormat::RGBA16F);
    s.flashlight_intensity = light.intensity;
    s.flashlight_intensity_fps = light.intensity_fps;
    s.flashlight_size = light.size;
    s.flashlight_size_fps = light.size_fps;
    s.flashlight_inner_cos = light.inner_cos;
    s.flashlight_outer_cos = light.outer_cos;
    s.flashlight_range = light.range;
    s.flashlight_pos = {light.pos[0], light.pos[1], light.pos[2]};
    s.flashlight_dir = {light.dir[0], light.dir[1], light.dir[2]};
    s.flashlight_color = {light.color[0], light.color[1], light.color[2]};
    s.shadows = light.shadows && gfx.flashlight_shadows && light.per_pixel && light.active &&
                !render::feedback_control().present_last_frame;

    s.feedback_damp = render::feedback_control().damp_blend;
}

void fill_feedback(FeedbackSettings& f) {
    const render::FeedbackControl& control = render::feedback_control();
    f.dream_feedback = render::graphics().dream_feedback != 0;
    f.screen = control.screen != 0;
    f.exact = control.exact != 0;
    f.damp = control.damp;
    f.damp_blend = control.damp_blend;
    f.freeze_capture_hold = control.freeze_capture_hold != 0;
}

void fill_precision(precision::Settings& p) {
    const render::PreciseVertices& precise = render::precise();
    p.pgxp = precise.enabled != 0;
    p.near_clip = precise.near_clip != 0;
    p.near_z = precise.near_z;
    p.edge_max = precise.edge_max;
    p.poly_size_cull = precise.poly_size_cull != 0;
    p.poly_size_wide_scale = precise.poly_size_wide_scale;
    p.whole_map_far = render::whole_town().reproject_far != 0;
    p.debug = precise.debug;
    p.whole_map_probe = render::whole_town().probe;
    p.actor_depth = precise.actor_depth != 0;
    p.world_depth = precise.world_depth != 0;
    p.world_far_bias = precise.world_far_bias;
    p.weld_px = precise.weld_px;
    p.weld_w_ratio = precise.weld_w_ratio;
    p.screen_offset_x = precise.screen_offset_x;
    p.screen_offset_y = precise.screen_offset_y;
    p.projection_distance = precise.projection_distance;
}

// What the game reads back: the last on-screen split's view and the target sizes.
void publish(Renderer& r) {
    const Targets& t = r.targets();
    const ViewLatches& l = t.latches();
    render::PublishedView& view = render::published();
    std::copy(l.world_ortho_rect.begin(), l.world_ortho_rect.end(), view.world_ortho_rect);
    std::copy(l.world_ortho.begin(), l.world_ortho.end(), view.world_ortho);
    view.world_disp[0] = l.world_disp[0];
    view.world_disp[1] = l.world_disp[1];
    view.world_ortho_valid = l.world_ortho_valid ? 1 : 0;
    view.feedback_wide_scale = l.feedback_wide_scale;
    view.feedback_wide_center = l.feedback_wide_center;
    view.scene_width = t.width();
    view.scene_height = t.height();
    r.host().drawable_size(&view.window_width, &view.window_height);
}

overlay::Frame overlay_frame(int target_w, int target_h, int px, int py, int pw, int ph) {
    overlay::Frame f;
    f.target_w = target_w;
    f.target_h = target_h;
    f.picture = {static_cast<float>(px), static_cast<float>(py), static_cast<float>(pw), static_cast<float>(ph)};
    return f;
}

void record_overlay(Renderer& r, rhi::CommandList& cmd, rhi::TextureH target, int w, int h) {
    overlay::Overlay& o = overlay::overlay();
    if (o.list().empty() || w <= 0 || h <= 0) return;
    auto res = o.record(r.device(), cmd, target, static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    if (!res) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[GPU] overlay: %s", res.error().c_str());
    }
}

// The window overlay is drawn once, onto the window it was laid out for; then it waits for the next frame's layout.
void record_window_overlay(Renderer& r, rhi::CommandList& cmd, rhi::TextureH swap, std::uint32_t w, std::uint32_t h) {
    overlay::Overlay& o = overlay::window_overlay();
    if (!o.list().empty()) {
        auto res = o.record(r.device(), cmd, swap, w, h);
        if (!res) {
            static int s_logs = 0;
            if (s_logs++ < 4) SH_WARN("[GPU] window overlay: %s", res.error().c_str());
        }
    }
    o.begin_frame(overlay::Frame{});
}

}  // namespace

void begin_window_overlay() {
    Renderer* r = g_renderer.get();
    if (!r) return;
    int dw = 0, dh = 0, px = 0, py = 0, pw = 0, ph = 0;
    r->host().drawable_size(&dw, &dh);
    r->targets().present_rect(&px, &py, &pw, &ph);
    if (pw <= 0 || ph <= 0) {
        px = py = 0;
        pw = dw;
        ph = dh;
    }
    overlay::window_overlay().begin_frame(overlay_frame(dw, dh, px, py, pw, ph));
}

bool start(const DriverParams& params) {
    if (g_renderer) return true;
    InitParams init;
    init.window_name = params.window_name;
    init.width = params.width;
    init.height = params.height;
    init.fullscreen = params.fullscreen;
    init.msaa = params.msaa;
    init.backend = params.backend;
    init.vulkan_validation = params.vulkan_validation;
    init.pipeline_cache_dir = params.pipeline_cache_dir;
    init.game_version = params.game_version;
    init.shader_prewarm = params.shader_prewarm;
    init.software_gl = params.software_gl;
    g_renderer = Renderer::create(init);
    if (!g_renderer) return false;
    Renderer& r = *g_renderer;
    set_renderer(&r);
    PageHooks& pages = page_hooks();
    pages.lookup_override = params.lookup_override;
    pages.note_feedback_reader = [](bool semi, int r0) {
        if (Renderer* live = renderer()) live->feedback().note_sampler_prim(semi, r0);
    };
    // At native resolution the overlay lands on the window over the scaled picture, in the same 4:3 rect, so the
    // pointer fractions line up.
    r.post().set_after_present([](rhi::CommandList& cmd, rhi::TextureH swap, std::uint32_t w, std::uint32_t h) {
        if (g_overlay_native && g_renderer)
            record_overlay(*g_renderer, cmd, swap, static_cast<int>(w), static_cast<int>(h));
        if (g_renderer) record_window_overlay(*g_renderer, cmd, swap, w, h);
    });
    sync_state();
    window_changed();
    publish(r);
    return true;
}

void stop() {
    if (!g_renderer) return;
    overlay::overlay().release_gpu();
    overlay::window_overlay().release_gpu();
    set_renderer(nullptr);
    g_renderer.reset();
}

void sync_state() {
    Renderer* r = g_renderer.get();
    if (!r) return;
    // Precision first: the shade inputs read its SZ normaliser.
    fill_precision(precision::settings());
    fill_view(r->targets().view());
    fill_shade(r->splits().shade(), *r);
    fill_feedback(r->feedback().settings());
    r->shadow().set_settings(shadow_settings_from_state());
    r->textures().set_filter(render::graphics().texture_filter);
}

void frame_begin() {
    Renderer* r = g_renderer.get();
    if (!r) return;
    follow_window();
    sync_state();
    r->begin_frame();
    r->feedback().settle_capture(r->host().frame(), render::feedback_control().present_last_frame != 0);
    // Clear every frame, not only when the environment asks for it: in state transitions (door fades, pickups,
    // wipes) a frame can draw before its environment is set, and the previous frame would show through.
    const DrawEnv& env = r->draw_env();
    const std::uint8_t cr = env.clear_bg ? env.bg_r : 0, cg = env.clear_bg ? env.bg_g : 0,
                       cb = env.clear_bg ? env.bg_b : 0;
    r->clear(env.clip, cr, cg, cb);
    // While the game is frozen (pause, console, map message) the last captured frame is drawn again, so this
    // frame's UI lands on the frozen world: the PS1 never cleared its framebuffer.
    if (render::feedback_control().present_last_frame) r->feedback().present_last_frame(r->host().frame());
}

void set_overlay(void (*overlay)()) { g_overlay_hook = overlay; }

void frame_end() {
    Renderer* r = g_renderer.get();
    if (!r) return;
    FeedbackLogic& fb = r->feedback().logic();
    render::FeedbackControl& control = render::feedback_control();
    if (control.hold_frame) fb.set_hold_frame();
    control.hold_frame = 0;
    if (control.store_allowed > 0) fb.allow_store(control.store_allowed);
    control.store_allowed = 0;
    fb.set_smooth_trail(control.smooth_trail != 0);
    control.smooth_trail = 0;
    // A per-frame opt-out: the game sets it again each tick of a screen that protects its pages (the paper map).
    fb.set_skip_store(control.skip_store != 0);
    control.skip_store = 0;
    sync_state();
    r->end_scene();
    // The scene the game described for this frame is done (the next one starts empty).
    ShScene_FrameEnd();

    Targets& t = r->targets();
    begin_window_overlay();
    g_overlay_native = render::picture().native_render != 0;
    if (g_overlay_native) {
        int dw = 0, dh = 0, px = 0, py = 0, pw = 0, ph = 0;
        r->host().drawable_size(&dw, &dh);
        t.present_rect(&px, &py, &pw, &ph);
        overlay::overlay().begin_frame(overlay_frame(dw, dh, px, py, pw, ph));
        t.set_overlay_to_window(true);
        if (g_overlay_hook) g_overlay_hook();
        t.set_overlay_to_window(false);
    } else {
        int px = 0, py = 0, pw = 0, ph = 0;
        t.picture_rect(&px, &py, &pw, &ph);
        overlay::overlay().begin_frame(overlay_frame(t.width(), t.height(), px, py, pw, ph));
        if (g_overlay_hook) g_overlay_hook();
        // 2D on top of the finished world: drawn into the single-sample picture, not multisampled.
        rhi::CommandList& cmd = r->host().frame();
        if (!overlay::overlay().list().empty()) {
            record_overlay(*r, cmd, t.resolved(cmd), t.width(), t.height());
            t.wrote_resolved();
        }
    }

    r->present();
    // SH_GPU_STATS=1: the split renderer's counters every two seconds (what reached the device).
    static const bool s_stats = std::getenv("SH_GPU_STATS") != nullptr;
    static std::uint64_t s_frames = 0;
    if (s_stats && (++s_frames % 120) == 0) {
        const SplitRenderer::Stats& st = r->splits().stats();
        SH_LOG("[GPU] splits %llu draws %llu scenes %llu failures %llu", static_cast<unsigned long long>(st.splits),
               static_cast<unsigned long long>(st.draws), static_cast<unsigned long long>(st.scenes),
               static_cast<unsigned long long>(st.failures));
    }
    sh::frame::frame().end_frame();
    if (render::picture().ui2d_wide_frames > 0) --render::picture().ui2d_wide_frames;
    publish(*r);
    for (auto* listener : g_frame_end_listeners) listener();
}

void set_swap_interval(int interval) {
    if (g_renderer) g_renderer->host().set_swap_interval(interval);
}

void follow_window() {
    Renderer* r = g_renderer.get();
    if (!r) return;
    int dw = 0, dh = 0;
    r->host().drawable_size(&dw, &dh);
    if (dw > 0 && dh > 0 && (dw != r->targets().present_width() || dh != r->targets().present_height()))
        window_changed();
}

void window_changed() {
    Renderer* r = g_renderer.get();
    if (!r) return;
    fill_view(r->targets().view());
    // In back-buffer pixels: the present blits into the swapchain, which is pixels whatever the display scale.
    int w = 0, h = 0;
    r->host().drawable_size(&w, &h);
    r->targets().resize(w, h);
    r->host().reset();
    publish(*r);
}

void add_frame_end_listener(void (*listener)()) {
    if (listener && std::find(g_frame_end_listeners.begin(), g_frame_end_listeners.end(), listener) ==
                        g_frame_end_listeners.end())
        g_frame_end_listeners.push_back(listener);
}

}  // namespace sh::gpu

namespace sh::gpu {

bool window_to_picture(int mx, int my, float* frac_x, float* frac_y) {
    Renderer* r = renderer();
    float fx = 0.f, fy = 0.f;
    if (r) {
        // The pointer comes in window units, the present rect is in back-buffer pixels (they differ on a
        // high-density display).
        int ww = 0, wh = 0, pw = 0, ph = 0;
        r->host().window_size(&ww, &wh);
        r->host().drawable_size(&pw, &ph);
        if (ww > 0 && wh > 0 && pw > 0 && ph > 0 && (ww != pw || wh != ph)) {
            mx = static_cast<int>(static_cast<long long>(mx) * pw / ww);
            my = static_cast<int>(static_cast<long long>(my) * ph / wh);
        }
    }
    const bool in = r && r->targets().window_to_viewport(mx, my, &fx, &fy);
    if (frac_x) *frac_x = fx;
    if (frac_y) *frac_y = fy;
    return in;
}

void window_to_overlay(int mx, int my, float* x, float* y) {
    present::PointF p{static_cast<float>(mx), static_cast<float>(my)};
    if (Renderer* r = renderer()) {
        present::PointerInput in;
        r->host().window_size(&in.window_w, &in.window_h);
        r->host().drawable_size(&in.drawable_w, &in.drawable_h);
        in.overlay_to_window = g_overlay_native;
        const Targets& t = r->targets();
        t.present_rect(&in.present.x, &in.present.y, &in.present.w, &in.present.h);
        in.target_w = t.width();
        in.target_h = t.height();
        p = present::pointer_to_overlay(in, p.x, p.y);
    }
    if (x) *x = p.x;
    if (y) *y = p.y;
}

void picture_viewport(int* x, int* y, int* w, int* h) {
    int vx = 0, vy = 0, vw = 0, vh = 0;
    if (Renderer* r = renderer()) r->targets().display_viewport(&vx, &vy, &vw, &vh);
    if (x) *x = vx;
    if (y) *y = vy;
    if (w) *w = vw;
    if (h) *h = vh;
}

void read_video_memory(unsigned short* dst, int x, int y, int w, int h) {
    if (Renderer* r = renderer())
        r->store_image({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(w),
                        static_cast<std::int16_t>(h)},
                       dst);
}

}  // namespace sh::gpu
