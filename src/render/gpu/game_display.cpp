#include "render/gpu/game_display.h"

#include <array>
#include <cstddef>
#include <variant>

#include "render/gpu/feedback_pass.h"
#include "render/gpu/frame_draw.h"
#include "render/gpu/frame_driver.h"
#include "render/gpu/render_queries.h"
#include "render/gpu/render_state.h"
#include "render/gpu/renderer.h"
#include "render/gpu/targets.h"
#include "render/gpu/vertex_precision.h"

namespace sh::gpu::display {

namespace {

// Inputs of a layer draw the entries do not carry (render globals the game changes between its layer draws).
struct LayerSettings {
    int ui2d_slot = -1;
    bool frame_3d = false;
    bool force_item_depth = false;
    bool pgxp = false;
    int view_z_shift = 0;
    bool dream_feedback = true;
    bool poly_size_cull = true;
    bool whole_map_far = false;
    float poly_size_wide_scale = 1.f;
    bool view_space = false;
    bool world_depth = true;
    bool pp_lit = false;
    WideRules wide;
};

// The engine draws both buffers at the video-memory origin with the draw offset at the screen centre: the PS1's
// double buffering is the renderer's business.
struct Buffers {
    std::array<DrawEnv, 2> draw{};
    std::array<DispEnv, 2> disp{};
    int active = 0;
    int width = 320;
    int height = 240;
    // The game's copy of the draw environment: only its clear colour reaches the buffers, at each swap.
    bool copy_clear = true;
    std::uint8_t copy_r = 0, copy_g = 0, copy_b = 0;
};

LayerSettings g_settings;
FrameHooks g_hooks;
Buffers g_buffers;
bool g_masked = false;      // drawing disabled
bool g_presenting_again = false;
bool g_item_pass = false;   // between item_depth_begin and item_depth_end
DispEnv g_last_disp{};      // the environment last put; the shown one lags it by a swap
bool g_last_disp_set = false;

// Where the last layer draw stopped: its depth seed and slot (a command drawn at once keeps them).
float g_slot_z = -1.f;
int g_slot = -1;

constexpr std::uint32_t kFlushRoom = 24;  // room for the largest single command (a line strip: 18 vertices)

void discard() { builder().discard(); }

bool begin_draw() {
    if (g_masked) {
        discard();
        return false;
    }
    if (g_hooks.begin_scene != nullptr && g_hooks.begin_scene()) discard();
    return true;
}

void put_draw(const DrawEnv& env) {
    Renderer* r = renderer();
    if (r == nullptr) return;
    builder().state().env = env;
    r->set_draw_env(env);
}

void put_disp(const DispEnv& env) {
    Renderer* r = renderer();
    if (r == nullptr) return;
    // The shown buffer is the one put a swap earlier, except when the mode changes or the same buffer is put again.
    DispEnv shown;
    if (!g_last_disp_set || env.interlaced != g_last_disp.interlaced || env.disp.y == g_last_disp.disp.y) {
        shown = env;
    } else {
        shown = g_last_disp;
    }
    g_last_disp = env;
    g_last_disp_set = true;
    builder().state().disp = shown;
    r->set_disp_env(shown);
}

void reset_buffers() {
    Buffers& b = g_buffers;
    for (std::size_t i = 0; i < 2; ++i) {
        DispEnv& d = b.disp[i];
        d = DispEnv{};
        d.disp = Rect{0, 0, static_cast<std::int16_t>(b.width), static_cast<std::int16_t>(b.height)};
        DrawEnv& e = b.draw[i];
        e = DrawEnv{};
        e.clip = d.disp;
        e.ofs_x = static_cast<std::int16_t>(b.width / 2);
        e.ofs_y = static_cast<std::int16_t>(b.height / 2);
        e.tpage = 10;
        e.dither = true;
        e.draw_on_display = true;
        e.clear_bg = true;
    }
    b.copy_clear = true;
    b.copy_r = b.copy_g = b.copy_b = 0;
}

std::uint32_t feedback_sample(bool scratch, int page_x, int page_y, int u0, int v0, int dst_x, int dst_y, int w,
                              int h, int* mode, int* off_x, int* off_y, int* nat_w, int* nat_h) {
    Renderer* r = renderer();
    if (r == nullptr) return 0;
    return r->feedback().sample_texture(scratch, page_x, page_y, u0, v0, dst_x, dst_y, w, h, mode, off_x, off_y,
                                        nat_w, nat_h);
}

// Reads the render globals of a draw (the game changes some between its layer draws: the UI pass, the item pass).
void refresh_settings() {
    sync_state();
    LayerSettings& s = g_settings;
    s.ui2d_slot = sh::render::frame_state().ui2d_bucket;
    s.frame_3d = sh::render::frame_state().world_3d != 0;
    s.force_item_depth = sh::render::frame_state().force_item_depth != 0 || g_item_pass;
    s.pgxp = sh::render::precise().enabled != 0;
    s.wide.ui_ortho_pass = sh::render::frame_state().ui_ortho_pass != 0;
    if (Renderer* r = renderer()) {
        const ViewLatches& l = r->targets().latches();
        s.wide.world_ortho_valid = l.world_ortho_valid;
        for (int i = 0; i < 4; ++i) s.wide.world_ortho[i] = l.world_ortho[static_cast<std::size_t>(i)];
        s.wide.world_disp[0] = l.world_disp[0];
        s.wide.world_disp[1] = l.world_disp[1];
        s.wide.feedback_wide_scale = l.feedback_wide_scale;
        s.wide.feedback_wide_center = l.feedback_wide_center;
    }
    s.dream_feedback = sh::render::graphics().dream_feedback != 0;
    s.poly_size_cull = sh::render::precise().poly_size_cull != 0;
    s.whole_map_far = sh::render::whole_town().reproject_far != 0;
    s.poly_size_wide_scale = sh::render::precise().poly_size_wide_scale;
    s.view_space = sh::render::view_space_wanted();
    s.world_depth = sh::render::precise().world_depth != 0;

    precision::Settings& p = precision::settings();
    p.pgxp = s.pgxp;
    p.force_item_depth = s.force_item_depth;
    p.near_clip = sh::render::precise().near_clip != 0;
    p.near_z = sh::render::precise().near_z;
    p.edge_max = sh::render::precise().edge_max;
    p.poly_size_cull = s.poly_size_cull;
    p.poly_size_wide_scale = s.poly_size_wide_scale;
    p.whole_map_far = s.whole_map_far;
    p.debug = sh::render::precise().debug;

    builder().state().feedback_damp = sh::render::feedback_control().damp_blend;
    builder().set_mixed_program(sh::render::debug_state().mixed_program != 0);
}

FrameDrawContext draw_context() {
    const LayerSettings& s = g_settings;
    FrameDrawContext c;
    c.layer_depth = g_slot_z;
    c.frame_3d = s.frame_3d;
    c.force_item_depth = s.force_item_depth;
    c.ui2d_layer = s.ui2d_slot >= 0 && g_slot == s.ui2d_slot;
    c.pgxp = s.pgxp;
    c.poly_size_cull = s.poly_size_cull;
    c.whole_map_far = s.whole_map_far;
    c.poly_size_wide_scale = s.poly_size_wide_scale;
    c.view_space = s.view_space;
    c.world_depth = s.world_depth;
    c.pp_lit = s.pp_lit;
    c.dream_feedback = s.dream_feedback;
    c.wide = s.wide;
    c.scene_redirect = &GR_SetSceneFbRedirect;
    c.feedback_sample = &feedback_sample;
    return c;
}

void draw_entry(SplitBuilder& out, const sh::frame::Entry& e) {
    out.flush_if_near_full(kFlushRoom);
    draw_frame_entry(out, e, draw_context());
}

// Start of a layer draw: the frame's depth maximum rolls (what the frame's polygons fed it with this layer).
void roll_depth_max() {
    if (g_presenting_again) {
        precision::bump_generation();
        return;
    }
    precision::note_sz(sh::frame::state().take_item_depth());
    precision::begin_frame();
}

}  // namespace

SplitBuilder& builder() {
    static SplitBuilder s_builder([](SplitBatch& batch) {
        if (Renderer* r = renderer()) r->draw(batch);
    });
    return s_builder;
}

FrameHooks& frame_hooks() { return g_hooks; }

// ---- Display buffers ----

void init(int width, int height) {
    if (Renderer* r = renderer()) {
        g_masked = false;
        r->clear(builder().state().env.clip, 0, 0, 0);
        discard();
        if (g_hooks.end_scene != nullptr) g_hooks.end_scene();
    }
    g_buffers.width = width;
    g_buffers.height = height;
    g_buffers.active = 0;
    reset_buffers();
}

void set_display_pages(int x0, int y0, int x1, int y1) {
    if (Renderer* r = renderer())
        r->feedback().set_display_buffers(x0, y0, x1, y1, g_buffers.width, g_buffers.height);
    reset_buffers();
}

void swap_buffers() {
    Buffers& b = g_buffers;
    b.active ^= 1;
    DrawEnv& d = b.draw[static_cast<std::size_t>(b.active)];
    d.clear_bg = b.copy_clear;
    d.bg_r = b.copy_r;
    d.bg_g = b.copy_g;
    d.bg_b = b.copy_b;
    d.draw_on_display = true;
    put_disp(b.disp[static_cast<std::size_t>(b.active)]);
    put_draw(d);
}

int active_buffer() { return g_buffers.active; }

void set_presenting_again(bool again) { g_presenting_again = again; }

void sort_clear(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    DrawEnv& d = g_buffers.draw[static_cast<std::size_t>(g_buffers.active)];
    d.bg_r = r;
    d.bg_g = g;
    d.bg_b = b;
    d.clear_bg = true;
    put_draw(d);
}

void set_display_enabled(bool enabled) { g_masked = !enabled; }

// ---- Layers ----

void set_view_z_shift(int shift) { g_settings.view_z_shift = shift; }

void clear_layer(sh::frame::Layer layer) { sh::frame::frame().clear(layer, sh::frame::build_buffer()); }

void draw_layer(sh::frame::Layer layer) {
    // Each layer tests depth only against itself; the depth maximum rolls with it.
    roll_depth_max();
    if (Renderer* r = renderer()) {
        builder().flush();
        r->clear_depth();
    }
    if (renderer() == nullptr || !begin_draw()) return;
    // After begin_draw: its first call of a frame starts the frame, which resets the world's jitter and scale.
    if (layer == sh::frame::Layer::World) renderer()->begin_world();
    refresh_settings();
    SplitBuilder& out = builder();
    out.batch().force_item_depth = g_settings.force_item_depth;
    out.state().mask_mode = 0;
    const int slots = static_cast<int>(sh::frame::slot_count(layer));
    int walked = 0;  // slots entered from the top; a linked inset's slots count on past the world layer's bottom
    g_slot = -1;
    g_slot_z = -1.f;
    auto step = [&] {
        g_slot_z = slot_depth(walked, slots, g_settings.view_z_shift);
        g_slot = slots - 1 - walked;
        ++walked;
    };
    sh::frame::frame().take_layer(
        layer, sh::frame::build_buffer(), [&](std::uint32_t, bool) { step(); },
        [&](const sh::frame::Entry& e) {
            if (e.steps_down) step();
            draw_entry(out, e);
        });
    out.flush();
    if (layer == sh::frame::Layer::World) {
        if (Renderer* r = renderer()) r->end_world();
    }
}

void prepare_world_layer() {
    resolve_scene_captures(sh::frame::frame(), sh::frame::build_buffer(), &GR_SetSceneFbRedirect);
}

void draw_now(const sh::draw2d::Command& command) {
    if (renderer() == nullptr || !begin_draw()) return;
    refresh_settings();
    SplitBuilder& out = builder();
    // A lone command draws in the mask-write mode of a single primitive, at the depth the last layer draw ended on.
    out.state().mask_mode = 1;
    sh::frame::Entry e;
    if (const auto* area = std::get_if<sh::draw2d::DrawArea>(&command)) {
        e.kind = sh::frame::Kind::Area;
        e.rect = {area->x, area->y, area->w, area->h};
    } else {
        e.kind = sh::frame::Kind::Draw2D;
        e.id = sh::draw2d::frame_commands().record(command);
    }
    draw_entry(out, e);
}

void item_depth_begin() {
    g_item_pass = true;
    // The isolated item is drawn on an empty depth buffer: its own front faces hide its back faces.
    if (Renderer* r = renderer()) {
        builder().flush();
        r->clear_depth();
    }
}

void item_depth_end() { g_item_pass = false; }

// ---- Video memory and sync ----

void sync() {
    if (renderer() != nullptr) builder().flush();
}

// Video memory keeps its words while the frame is drawn on screen instead of into it: textures and palettes share
// those addresses, so only the drawn picture is cleared.
void clear_image(const Rect& rect, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (Renderer* gpu = renderer()) gpu->clear(rect, r, g, b);
}

}  // namespace sh::gpu::display
