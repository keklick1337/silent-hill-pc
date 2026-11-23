#include "render/gpu/frame_draw.h"

#include "render/draw2d/draw2d_stream.h"
#include "render/draw3d/draw3d_stream.h"
#include "render/gpu/page_resolve.h"
#include "render/gpu/vertex_precision.h"

#include <cmath>
#include <span>

namespace sh::gpu {

namespace {

// Opens the primitive's split and makes room for `count` vertices; a full batch is drawn first and the split
// reopened in the next one (the page state of the primitive survives the flush).
Vertex* open_run(SplitBuilder& out, bool semi, bool textured, DepthMode depth, std::uint32_t count) {
    out.begin_prims(semi, textured, depth);
    Vertex* v = out.reserve(count);
    if (v == nullptr) {
        out.flush();
        out.begin_prims(semi, textured, depth);
        v = out.reserve(count);
    }
    return v;
}

bool marks_geometry(const FrameDrawContext& ctx) { return ctx.frame_3d && !ctx.force_item_depth && !ctx.ui2d_layer; }

bool samples_display_band(std::uint16_t tpage) { return ((tpage >> 7) & 3) >= 2 && (tpage & 0xF) * 64 < 320; }

// The 2D draw list at its place in the frame.
class Draw2DTarget final : public sh::draw2d::StreamTarget {
public:
    Draw2DTarget(SplitBuilder& out, const FrameDrawContext& ctx, const void* key) : out_(out), ctx_(ctx), key_(key) {}

    sh::draw2d::StreamFrame frame() const override {
        const PrimState& st = out_.state();
        sh::draw2d::StreamFrame f;
        f.offset_x = static_cast<int>(sh::frame::state().draw_offset_x);
        f.offset_y = static_cast<int>(sh::frame::state().draw_offset_y);
        f.depth = ctx_.layer_depth;
        f.page = st.env.tpage;
        f.dither = st.env.dither;
        f.mark_geometry = marks_geometry(ctx_);
        f.precise = ctx_.pgxp;
        f.size_rule = ctx_.poly_size_cull && !ctx_.whole_map_far;
        f.max_poly_width = 1023.f * ctx_.poly_size_wide_scale;
        return f;
    }

    void latch_page(std::uint16_t page) override { out_.state().env.tpage = page; }

    void latch_mode(std::uint16_t page, bool dither) override {
        DrawEnv& env = out_.state().env;
        env.tpage = page;
        env.dither = dither;
        env.draw_on_display = true;  // the dfe bit only matters for interlace: every mode change draws on screen
        publish_draw_env(out_.state());
    }

    void latch_area(const sh::draw2d::DrawArea& a) override {
        latch_draw_area(out_, a.x & 1023, a.y & 1023, (a.x + a.w) & 1023, (a.y + a.h) & 1023, ctx_.scene_redirect);
    }

    // The mask bit is not modelled.
    void latch_mask(const sh::draw2d::Mask&) override {}

    rhi::PrimVertex* begin(const sh::draw2d::StreamRun& run, int vertices) override {
        using sh::draw2d::RunShape;
        using sh::draw2d::RectSize;
        out_.begin_primitive(run.translucent, run.color.r);
        PrimState& st = out_.state();
        const DepthMode depth = ctx_.split_depth != nullptr ? ctx_.split_depth(key_) : DepthMode::Disabled;
        const std::uint16_t clut = run.palette.word();
        bool textured = false;
        switch (run.shape) {
            case RunShape::Polygon:
                textured = run.textured;
                if (textured && !resolve_page(out_, run.page, clut, "POLY2D")) return nullptr;
                break;
            case RunShape::Sprite:
                textured = true;
                if (!resolve_page(out_, st.env.tpage, clut, "SPRT2D")) return nullptr;
                if (run.size == RectSize::Free) {
                    if (st.scratch && !ctx_.dream_feedback) return nullptr;
                    if ((st.feedback || st.scratch) && st.override_texture.kind == TextureRef::Kind::White)
                        sample_feedback(st, ctx_.feedback_sample, run.at.x, run.at.y, run.uv.u, run.uv.v, run.w,
                                        run.h);
                }
                break;
            case RunShape::Fill:
                // The lighthouse glow's add/subtract fills follow its feedback sprites across the widened UI ortho.
                if (run.size == RectSize::Free && run.translucent && run.h >= 224 && samples_display_band(st.env.tpage))
                    st.feedback = true;
                break;
            case RunShape::Line:
                break;
        }
        const auto room = static_cast<std::uint32_t>(vertices);
        Vertex* v = open_run(out_, run.translucent, textured, depth, room);
        if (v != nullptr) first_ = out_.vertex_count() - room;  // after a flush the run starts a new batch
        return v;
    }

    void shape(const sh::draw2d::StreamRun& run, rhi::PrimVertex* corners, int) override {
        using sh::draw2d::RunShape;
        using sh::draw2d::RectSize;
        const PrimState& st = out_.state();
        if (run.shape == RunShape::Sprite || run.shape == RunShape::Fill) rect_wide_stretch(corners, st, ctx_.wide);
        if (run.shape == RunShape::Fill && run.size == RectSize::Free) stretch_full_frame_fill(corners, ctx_.wide);
        if (run.shape == RunShape::Sprite && run.size == RectSize::Free) scratch_damp(corners, st);
    }

    void end(int vertices) override { out_.truncate(first_ + static_cast<std::uint32_t>(vertices)); }

private:
    SplitBuilder& out_;
    const FrameDrawContext& ctx_;
    const void* key_;
    std::uint32_t first_ = 0;
};

// The world-space effect list at its place in the frame.
class Draw3DTarget final : public sh::draw3d::StreamTarget {
public:
    Draw3DTarget(SplitBuilder& out, const FrameDrawContext& ctx) : out_(out), ctx_(ctx) {}

    sh::draw3d::PrimFlags begin_prim(const sh::draw3d::Polygon& p) override {
        out_.begin_primitive(p.shade.translucent, p.v[0].color.r);
        sh::draw3d::PrimFlags prim;
        // The flags only take effect on the precise path.
        prim.affine = ctx_.pgxp && p.affine;
        prim.no_flashlight = ctx_.pgxp && p.no_flashlight;
        depth_ = DepthMode::Disabled;
        if (ctx_.pgxp && ctx_.world_depth && (precision::settings().debug & 8) == 0 && p.depth &&
            p.depth->source == sh::draw3d::DepthSource::WorldMesh)
            depth_ = p.depth->far ? DepthMode::WorldFar : DepthMode::World;
        return prim;
    }

    void latch_page(std::uint16_t page) override { out_.state().env.tpage = page; }

    rhi::PrimVertex* begin(const sh::draw3d::StreamRun& run, int vertices) override {
        if (run.textured) {
            const std::uint16_t clut = run.palette.word();
            // A flat quad tests its palette before it latches its page; the other textured kinds latch first.
            const bool flat_quad = !run.gouraud && run.count == 4;
            if (flat_quad && clut_drops(run.page, clut, "FT4")) return nullptr;
            out_.state().env.tpage = run.page;
            const char* type = flat_quad ? "FT4" : run.gouraud ? (run.count == 4 ? "GT4" : "GT3") : "FT3";
            if (!resolve_page(out_, run.page, clut, type)) return nullptr;
        }
        // The near clip can turn each triangle into two.
        const auto room = static_cast<std::uint32_t>(vertices) * 2;
        Vertex* v = open_run(out_, run.translucent, run.textured, depth_, room);
        if (v != nullptr) first_ = out_.vertex_count() - room;
        return v;
    }

    sh::draw3d::StreamFrame frame() const override {
        const PrimState& st = out_.state();
        const precision::Settings& ps = precision::settings();
        sh::draw3d::StreamFrame f;
        f.offset_x = sh::frame::state().draw_offset_x;
        f.offset_y = sh::frame::state().draw_offset_y;
        f.depth = ctx_.layer_depth;
        f.page = st.env.tpage;
        f.dither = st.env.dither;
        f.mark_geometry = marks_geometry(ctx_);
        f.precise = ctx_.pgxp;
        f.view_space = ctx_.view_space && !ctx_.force_item_depth;
        f.edge_max = ps.edge_max;
        f.view_space_reach = 16384.f;
        f.near_clip = ps.near_clip;
        f.near_z = ps.near_z;
        return f;
    }

    void precise(rhi::PrimVertex& c, bool resolved) override {
        const int debug = precision::settings().debug;
        if ((debug & 1) != 0) {  // debug: every corner affine
            c.ppx = static_cast<float>(c.x);
            c.ppy = static_cast<float>(c.y);
            c.ppw = 0.f;
            return;
        }
        if (!resolved) return;
        precision::weld(&c.ppx, &c.ppy, &c.ppw);
        if ((debug & 64) != 0) c.ppw = 1.f;
        if ((debug & 128) != 0) {
            c.ppx = static_cast<float>(c.x);
            c.ppy = static_cast<float>(c.y);
        }
    }

    void view_projection(float ofx, float ofy, float h) override { precision::set_polygon_projection(ofx, ofy, h); }

    std::uint8_t depth(rhi::PrimVertex* corners, int count, const sh::draw3d::Polygon& p) override {
        precision::DepthIn in;
        in.found = p.depth.has_value();
        in.alpha = p.alpha;
        in.semi = p.shade.translucent;
        if (in.found) {
            const sh::draw3d::Depth& d = *p.depth;
            // A triangle's corners sit in slots 1..3 (the FIFO's SZ1..SZ3).
            if (count == 3) {
                in.sz[1] = d.sz[0];
                in.sz[2] = d.sz[1];
                in.sz[3] = d.sz[2];
            } else {
                for (int k = 0; k < 4; ++k) in.sz[k] = d.sz[static_cast<std::size_t>(k)];
            }
            switch (d.source) {
                case sh::draw3d::DepthSource::Projection: in.kind = precision::DepthKind::None; break;
                case sh::draw3d::DepthSource::WorldMesh: in.kind = precision::DepthKind::Flat; break;
                case sh::draw3d::DepthSource::Exact: in.kind = precision::DepthKind::Exact; break;
                case sh::draw3d::DepthSource::Actor: in.kind = precision::DepthKind::Actor; break;
            }
            in.far = d.far;
        }
        return precision::apply_depth(corners, in, count == 4);
    }

    void end(const sh::draw3d::StreamRun& run, rhi::PrimVertex* triangles, int vertices) override {
        const bool lit = ctx_.pp_lit && run.light != sh::draw3d::LightKind::None;
        // What the near clipper or the size rule reshapes does not line up with the corners' normals.
        const bool reshaped = lit && ctx_.pgxp && precision::near_clip_eligible(triangles, vertices);
        const int emitted = precision::emit_polygon(triangles, vertices);
        out_.truncate(first_ + static_cast<std::uint32_t>(emitted));
        if (!ctx_.pp_lit || !run.textured || !run.gouraud) return;
        if (lit && !reshaped && emitted == vertices) {
            static constexpr int kTriCorners[3] = {0, 1, 2};
            static constexpr int kQuadCorners[6] = {0, 1, 2, 1, 2, 3};
            float normals[12];
            for (int k = 0; k < 4; ++k) {
                normals[k * 3 + 0] = run.normals[static_cast<std::size_t>(k)].x;
                normals[k * 3 + 1] = run.normals[static_cast<std::size_t>(k)].y;
                normals[k * 3 + 2] = run.normals[static_cast<std::size_t>(k)].z;
            }
            precision::lit_fill(std::span<const float>(normals), run.light == sh::draw3d::LightKind::World ? 1 : 2,
                                first_, vertices, vertices == 3 ? kTriCorners : kQuadCorners);
        } else {
            precision::lit_zero_to(first_ + static_cast<std::uint32_t>(emitted));
        }
    }

private:
    SplitBuilder& out_;
    const FrameDrawContext& ctx_;
    DepthMode depth_ = DepthMode::Disabled;
    std::uint32_t first_ = 0;
};

}  // namespace

void draw_frame_entry(SplitBuilder& out, const sh::frame::Entry& e, const FrameDrawContext& ctx) {
    switch (e.kind) {
        case sh::frame::Kind::Draw2D:
            if (const sh::draw2d::Command* c = sh::draw2d::frame_commands().find(e.id & sh::draw2d::kIdMask)) {
                Draw2DTarget target(out, ctx, e.key);
                sh::draw2d::emit_stream(*c, target);
            }
            break;
        case sh::frame::Kind::Draw3D:
            if (const sh::draw3d::Command* c = sh::draw3d::frame_commands().find(e.id & sh::draw3d::kIdMask)) {
                Draw3DTarget target(out, ctx);
                sh::draw3d::emit_stream(*c, target);
            }
            break;
        case sh::frame::Kind::Scene:
            // Item code 0 is the map cells (-1 for the scene renderer), n + 1 the posed mesh n.
            out.add_scene(static_cast<std::int32_t>(e.id) - 1, sh::frame::state().draw_offset_x,
                          sh::frame::state().draw_offset_y);
            break;
        case sh::frame::Kind::Capture:
            out.add_capture(Rect{e.rect.x, e.rect.y, e.rect.w, e.rect.h});
            break;
        case sh::frame::Kind::Area: {
            // The flags the area change carries as a primitive: a translucent code, the corner's low byte as red.
            out.begin_primitive(true, static_cast<std::uint8_t>(e.rect.x));
            const int x = e.rect.x, y = e.rect.y;
            latch_draw_area(out, x & 1023, y & 1023, (x + e.rect.w) & 1023, (y + e.rect.h) & 1023, ctx.scene_redirect);
            break;
        }
    }
}

float slot_depth(int walked, int slots, int view_z_shift) {
    constexpr float kLinearDepthRange = 262144.f;  // view Z mapped onto NDC by the linear depth scale (2^18 SZ units)
    if (view_z_shift > 0) {
        // Slot b covers view Z ~ b << shift: seeded on the linear scale the tracked primitives use, untracked content
        // keeps its painter place.
        int b = slots - 1 - walked;
        if (b < 0) b = 0;
        const float vz = static_cast<float>(static_cast<unsigned>(b) << view_z_shift) +
                         0.5f * static_cast<float>(1 << view_z_shift);
        return std::fmin(1.f, std::fmax(-1.f, 1.f - 2.f * vz / kLinearDepthRange));
    }
    const float step = slots > 1 ? 2.f / static_cast<float>(slots - 1) : 0.f;
    const float z = -1.f + static_cast<float>(walked) * step;
    return z > 1.f ? 1.f : z;
}

void resolve_scene_captures(sh::frame::Frame& frame, int buffer, void (*scene_redirect)(int, int, int, int)) {
    constexpr int kOffscreenX = 320;  // both display buffers lie left of it
    int x = 0, y = 0, w = 0, h = 0;
    frame.visit_layer(sh::frame::Layer::World, buffer, [&](sh::frame::Entry& e) {
        if (e.kind != sh::frame::Kind::Area) return;
        const int x0 = e.rect.x & 1023, y0 = e.rect.y & 1023;
        const int x1 = (e.rect.x + e.rect.w) & 1023, y1 = (e.rect.y + e.rect.h) & 1023;
        if (x0 >= kOffscreenX && x1 > x0 && y1 > y0) {
            x = x0, y = y0, w = x1 - x0, h = y1 - y0;
            if (scene_redirect != nullptr) scene_redirect(x, y, w, h);
        } else if (w > 0) {
            e.kind = sh::frame::Kind::Capture;
            e.rect = {static_cast<std::int16_t>(x & 0x3FF), static_cast<std::int16_t>(y & 0x1FF),
                      static_cast<std::int16_t>(w), static_cast<std::int16_t>(h)};
            e.steps_down = true;
            w = 0;
        }
    });
}

// ---- Shared rules ----

void publish_draw_env(const PrimState& state) {
    sh::frame::state().set_draw_offset(state.env.draw_on_display, state.env.ofs_x - state.disp.disp.x,
                                       state.env.ofs_y - state.disp.disp.y);
}

void latch_draw_area(SplitBuilder& out, int x0, int y0, int x1, int y1, void (*scene_redirect)(int, int, int, int)) {
    DrawEnv& env = out.state().env;
    env.clip.x = static_cast<std::int16_t>(x0);
    env.clip.y = static_cast<std::int16_t>(y0);
    env.clip.w = static_cast<std::int16_t>(x1 - x0);
    env.clip.h = static_cast<std::int16_t>(y1 - y0);
    // Both display buffers live left of x 320: an area past it is offscreen video memory. A big one is a scene
    // rendered into video memory to be resampled (the renderer captures the frame there); a small one is a scratch
    // tile drawn offscreen and packed into video memory when drawing returns on screen.
    if (env.clip.x >= 320 && env.clip.w >= 64 && env.clip.h >= 64) {
        if (scene_redirect != nullptr) scene_redirect(env.clip.x, env.clip.y, env.clip.w, env.clip.h);
        env.draw_on_display = true;
    } else if (env.clip.x >= 320 && env.clip.w > 0 && env.clip.h > 0) {
        env.draw_on_display = false;
    } else {
        env.draw_on_display = true;
    }
    publish_draw_env(out.state());
}

void rect_wide_stretch(Vertex* c, const PrimState& state, const WideRules& s) {
    // The scene-scratch soft focus is authored 320 wide in the world pass: stretched across the widened world
    // ortho (the capture is full width to match).
    if (state.scratch && !s.ui_ortho_pass && s.world_ortho_valid && s.world_disp[0] > 0.f) {
        const float mid = (s.world_ortho[0] + s.world_ortho[1]) * 0.5f;
        const float k = (s.world_ortho[1] - s.world_ortho[0]) / s.world_disp[0];
        if (k > 1.001f)
            for (int i = 0; i < 4; ++i)
                c[i].x = static_cast<std::int16_t>(std::floor(mid + (static_cast<float>(c[i].x) - mid) * k + 0.5f));
    }
    // Feedback strips only in the UI pass: the capture widens only for a UI-pass reader, and stretching the
    // world-pass loading trail over an unstretched capture would rescale the loop every frame.
    if (state.feedback && s.ui_ortho_pass && s.feedback_wide_scale > 1.001f)
        for (int i = 0; i < 4; ++i)
            c[i].x = static_cast<std::int16_t>(
                std::floor(s.feedback_wide_center +
                           (static_cast<float>(c[i].x) - s.feedback_wide_center) * s.feedback_wide_scale + 0.5f));
}

void stretch_full_frame_fill(Vertex* c, const WideRules& s) {
    if (s.ui_ortho_pass) {
        // The UI pass of a widened frame (a 2D screen over the whole window, Hor+): a fade or tint covering the
        // whole PS1 frame covers the window, so text drawn past the 4:3 frame fades with the rest.
        const float k = s.feedback_wide_scale, mid = s.feedback_wide_center;
        if (!(k > 1.001f) || !(mid > 0.f)) return;
        float x0 = c[0].x, x1 = c[0].x;
        for (int i = 1; i < 4; ++i) {
            x0 = std::fmin(x0, static_cast<float>(c[i].x));
            x1 = std::fmax(x1, static_cast<float>(c[i].x));
        }
        if (!(x0 <= 0.f && x1 >= 2.f * mid)) return;
        for (int i = 0; i < 4; ++i) {
            const float fx = mid + (static_cast<float>(c[i].x) - mid) * k;
            c[i].x = static_cast<std::int16_t>(static_cast<float>(c[i].x) <= x0 ? std::floor(fx) : std::ceil(fx));
        }
        return;
    }
    if (!s.world_ortho_valid) return;
    const float l = s.world_ortho[0], r = s.world_ortho[1], t = s.world_ortho[2], b = s.world_ortho[3];
    const float w = s.world_disp[0], h = s.world_disp[1];
    if (w <= 0.f || h <= 0.f || r <= l || b <= t) return;
    if (std::fabs(l) < 0.01f && std::fabs(r - w) < 0.01f && std::fabs(t) < 0.01f && std::fabs(b - h) < 0.01f) return;
    float x0 = c[0].x, x1 = c[0].x, y0 = c[0].y, y1 = c[0].y;
    for (int i = 1; i < 4; ++i) {
        x0 = std::fmin(x0, static_cast<float>(c[i].x));
        x1 = std::fmax(x1, static_cast<float>(c[i].x));
        y0 = std::fmin(y0, static_cast<float>(c[i].y));
        y1 = std::fmax(y1, static_cast<float>(c[i].y));
    }
    // A flat fill covering the whole PS1 frame (a scene tint, a flash) maps onto the world ortho's extent; a
    // textured full-frame sprite may be a 4:3 picture and is never stretched.
    if (!(x0 <= 0.f && x1 >= w && y0 <= 0.f && y1 >= h)) return;
    for (int i = 0; i < 4; ++i) {
        const float fx = l + static_cast<float>(c[i].x) * (r - l) / w;
        const float fy = t + static_cast<float>(c[i].y) * (b - t) / h;
        // Outward rounding: the ortho edges are fractional, an inward corner leaves an unfilled column.
        c[i].x = static_cast<std::int16_t>(static_cast<float>(c[i].x) <= x0 ? std::floor(fx) : std::ceil(fx));
        c[i].y = static_cast<std::int16_t>(static_cast<float>(c[i].y) <= y0 ? std::floor(fy) : std::ceil(fy));
    }
}

void scratch_damp(Vertex* c, const PrimState& state) {
    // dream_blur_strength: each blending soft-focus layer lerps toward its hardware result; average layers scale
    // their weight (alpha), additive ones their colour. The opaque base layer is handled by the split's blend.
    if (!state.scratch || !state.semi || state.feedback_damp >= 1.f) return;
    const float s = state.feedback_damp;
    const bool average = ((state.env.tpage >> 5) & 3) == 0;
    auto scale = [s](std::uint8_t v) { return static_cast<std::uint8_t>(static_cast<float>(v) * s + 0.5f); };
    for (int i = 0; i < 4; ++i) {
        if (average) {
            c[i].a = scale(c[i].a);
        } else {
            c[i].r = scale(c[i].r);
            c[i].g = scale(c[i].g);
            c[i].b = scale(c[i].b);
        }
    }
}

void sample_feedback(PrimState& st, FeedbackSampleFn sample, int x, int y, int u, int v, int w, int h) {
    if (sample == nullptr) return;
    const std::uint16_t tpage = st.env.tpage;
    // The offset is absolute and the clip origin is the draw buffer's, so x + ofs - clip is where the sprite lands
    // in the buffer.
    const int dst_x = x + st.env.ofs_x - st.env.clip.x;
    const int dst_y = y + st.env.ofs_y - st.env.clip.y;
    int mode = 0, off_x = 0, off_y = 0, nat_w = 0, nat_h = 0;
    const std::uint32_t tex = sample(st.scratch, (tpage & 0xF) * 64, ((tpage >> 4) & 1) * 256, u, v, dst_x, dst_y, w,
                                     h, &mode, &off_x, &off_y, &nat_w, &nat_h);
    if (tex == 0 || mode == 0) return;
    st.override_texture = TextureRef{TextureRef::Kind::Feedback, tex, 0};
    st.override_info = OverrideInfo{};
    st.override_info.width = static_cast<std::int16_t>(nat_w);
    st.override_info.height = static_cast<std::int16_t>(nat_h);
    st.override_info.offset_x = static_cast<std::int16_t>(off_x);
    st.override_info.offset_y = static_cast<std::int16_t>(off_y);
    st.override_info.feedback_screen = mode | (st.semi ? 4 : 0);
}

}  // namespace sh::gpu
