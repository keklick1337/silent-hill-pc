#include "render/gpu/targets.h"

#include "engine/log/sh_log.h"
#include "render/frame/frame.h"
#include "render/gpu/shaders/ms_seed_shaders.h"
#include "render/present/present_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace sh::gpu {

namespace pr = sh::present;

namespace {

// The ortho near/far of every pass.
constexpr float kNear = -1.0f;
constexpr float kFar = 1.0f;

rhi::Rect to_rhi(const pr::PlacedRect& r) { return {r.x, r.y, r.w, r.h}; }

rhi::Viewport viewport_of(const pr::PlacedRect& r)
{
    rhi::Viewport vp;
    vp.x = static_cast<float>(r.x);
    vp.y = static_cast<float>(r.y);
    vp.w = static_cast<float>(r.w);
    vp.h = static_cast<float>(r.h);
    return vp;
}

}  // namespace

Targets::Targets(rhi::Device& device) : device_(device) {}

Targets::~Targets() { destroy(); }

Targets::Size Targets::scaled(int w, int h) const
{
    // Past 5K pixels supersampling costs far more than it shows, and a 4K window at render_scale 2 would ask for an
    // 8K target with its depth buffer.
    constexpr long long kMaxSupersamplePixels = 5120LL * 2880LL;
    const int max_side = static_cast<int>(std::min<std::uint32_t>(device_.caps().maxTextureSize, 1u << 30));
    const pr::Size s = pr::cap_supersampled_size(pr::scaled_render_size(w, h, view_.render_scale), w, h,
                                                 kMaxSupersamplePixels, max_side);
    return {s.w, s.h};
}

void Targets::release_scene()
{
    for (rhi::TextureH* t : {&scene_color_, &scene_depth_, &ms_color_, &ms_depth_}) {
        if (*t)
            device_.destroy(*t);
        *t = {};
    }
    samples_ = 1;
    ms_dirty_ = ms_stale_ = false;
}

void Targets::release_seed()
{
    if (seed_pipe_)
        device_.destroy(seed_pipe_);
    if (seed_fs_)
        device_.destroy(seed_fs_);
    if (seed_vs_)
        device_.destroy(seed_vs_);
    if (seed_triangle_)
        device_.destroy(seed_triangle_);
    if (seed_sampler_)
        device_.destroy(seed_sampler_);
    seed_pipe_ = {};
    seed_fs_ = seed_vs_ = {};
    seed_triangle_ = {};
    seed_sampler_ = {};
}

int Targets::wanted_samples() const
{
    const int cap = static_cast<int>(std::min<std::uint32_t>(device_.caps().maxSamples, 64u));
    return pr::msaa_sample_count(view_.msaa_samples, cap);
}

rhi::TextureFormat Targets::wanted_format() const {
    return view_.hdr_frame && device_.caps().floatTargets && !hdr_failed_ ? rhi::TextureFormat::RGBA16F
                                                                          : rhi::TextureFormat::RGBA8;
}

bool Targets::needs_resize() const
{
    return view_.render_scale != applied_scale_ || (scene_color_ && wanted_format() != scene_format_) ||
           (scene_color_ && sampled_depth_ != made_sampled_depth_) ||
           (scene_color_ && wanted_samples() != samples_ && !(samples_ == 1 && ms_failed_ == wanted_samples()));
}

void Targets::destroy()
{
    release_scene();
    release_seed();
    if (offscreen_color_)
        device_.destroy(offscreen_color_);
    if (solid_)
        device_.destroy(solid_);
    offscreen_color_ = {};
    solid_ = {};
    offscreen_w_ = offscreen_h_ = 0;
    width_ = height_ = 0;
}

bool Targets::set_internal_resolution(int w, int h)
{
    if (w <= 0 || h <= 0) {
        release_scene();
        width_ = height_ = 0;
        return false;
    }
    const int samples = wanted_samples();
    const rhi::TextureFormat format = wanted_format();
    if (scene_color_ && width_ == w && height_ == h && format == scene_format_ &&
        (samples == samples_ || ms_failed_ == samples) && sampled_depth_ == made_sampled_depth_)
        return true;

    release_scene();
    rhi::TextureDesc color;
    color.format = format;
    color.width = static_cast<std::uint32_t>(w);
    color.height = static_cast<std::uint32_t>(h);
    color.usage = rhi::kUsageSampled | rhi::kUsageColorTarget;
    color.name = "scene color";
    rhi::TextureDesc depth = color;
    depth.format = rhi::TextureFormat::D24S8;
    depth.usage = rhi::kUsageDepthStencilTarget | (sampled_depth_ ? rhi::kUsageSampled : 0u);
    depth.name = "scene depth";
    made_sampled_depth_ = sampled_depth_;

    auto c = device_.createTexture(color);
    auto d = device_.createTexture(depth);
    if (!c || !d) {
        if (c)
            device_.destroy(c.value());
        if (d)
            device_.destroy(d.value());
        if (format == rhi::TextureFormat::RGBA16F) {
            // Said once; the 8-bit frame of today takes over for the rest of the run.
            SH_WARN("[GPU] HDR scene target %dx%d: %s; hdr_frame is off", w, h, (!c ? c.error() : d.error()).c_str());
            hdr_failed_ = true;
            return set_internal_resolution(w, h);
        }
        width_ = height_ = 0;
        return false;
    }
    scene_color_ = c.value();
    scene_depth_ = d.value();
    scene_format_ = format;
    width_ = w;
    height_ = h;
    if (samples > 1)
        make_multisampled(samples);
    if (samples != logged_samples_) {
        logged_samples_ = samples;
        SH_LOG("[GPU] MSAA: %dx on the scene target (asked %d, device max %u)%s", samples_, view_.msaa_samples,
               device_.caps().maxSamples, samples_ != samples ? "; the multisampled target failed, off" : "");
    }
    return true;
}

void Targets::make_multisampled(int samples)
{
    rhi::TextureDesc color;
    color.format = scene_format_;
    color.width = static_cast<std::uint32_t>(width_);
    color.height = static_cast<std::uint32_t>(height_);
    color.usage = rhi::kUsageColorTarget;
    color.samples = static_cast<std::uint32_t>(samples);
    color.name = "scene color msaa";
    rhi::TextureDesc depth = color;
    depth.format = rhi::TextureFormat::D24S8;
    depth.usage = rhi::kUsageDepthStencilTarget;
    depth.name = "scene depth msaa";
    auto c = device_.createTexture(color);
    auto d = device_.createTexture(depth);
    if (!c || !d) {
        if (c)
            device_.destroy(c.value());
        if (d)
            device_.destroy(d.value());
        // Asked again only when the request or the size changes: a device that refused it once refuses it again.
        ms_failed_ = samples;
        SH_WARN("[GPU] %dx MSAA scene target: %s", samples, (!c ? c.error() : d.error()).c_str());
        return;
    }
    // The single-sample depth is never attached while the multisampled pair draws.
    device_.destroy(scene_depth_);
    scene_depth_ = {};
    ms_color_ = c.value();
    ms_depth_ = d.value();
    samples_ = samples;
    ms_failed_ = 0;
}

void Targets::set_world_layer(WorldLayer layer)
{
    layer.result = world_layer_.result;
    layer.sharpness = world_layer_.sharpness;
    layer.apply_result = std::move(world_layer_.apply_result);
    world_layer_ = std::move(layer);
}

void Targets::set_world_result(rhi::TextureH result, float sharpness, std::function<void()> apply)
{
    world_layer_.result = result;
    world_layer_.sharpness = sharpness;
    world_layer_.apply_result = std::move(apply);
}

void Targets::flush_world_layer()
{
    if (!world_layer())
        return;
    WorldLayer owed = std::move(world_layer_);
    world_layer_ = {};
    // The temporal result first: the atmosphere was left to go over it.
    if (owed.result && owed.apply_result)
        owed.apply_result();
    if (owed.layer && owed.apply)
        owed.apply();
}

void Targets::begin_draw(rhi::CommandList& cmd)
{
    // The owed end of the world reads the depth a depth-only clear still owed would wipe; a whole clear dropped it.
    flush_world_layer();
    flush_clear(cmd);
    if (samples_ <= 1)
        return;
    if (ms_stale_) {
        seed(cmd);
        ms_stale_ = false;
    }
    ms_dirty_ = true;
}

rhi::TextureH Targets::resolved(rhi::CommandList& cmd)
{
    // On a phone a read of the colour waits for a whole clear, not for one of the depth only: that one stays owed
    // until the next draw, so the end of a frame has no pass that only clears the depth (on a tile-based GPU a load
    // and store of the whole target). A computer clears it here: the ray tracer reads the scene depth before the
    // next frame's first draw.
    if (clear_pending_ || !phone_gpu())
        flush_clear(cmd);
    flush_world_layer();
    if (samples_ > 1 && ms_dirty_) {
        cmd.resolveTexture(ms_color_, scene_color_);
        ms_dirty_ = false;
    }
    return scene_color_;
}

rhi::TextureH Targets::resolved_unfinished(rhi::CommandList& cmd)
{
    WorldLayer owed = std::move(world_layer_);
    world_layer_ = {};
    const rhi::TextureH t = resolved(cmd);
    world_layer_ = std::move(owed);
    return t;
}

void Targets::wrote_resolved()
{
    world_layer_ = {};
    // Callers resolve before they write (or overwrite the whole picture), so nothing drawn is lost.
    ms_dirty_ = false;
    if (samples_ > 1)
        ms_stale_ = true;
}

bool Targets::seed_ready()
{
    if (seed_pipe_)
        return true;
    if (seed_failed_)
        return false;
    const auto fail = [this](std::string_view why) {
        seed_failed_ = true;
        release_seed();
        SH_WARN("[GPU] MSAA seed pass: %.*s; a frozen frame or a cleared picture rect may not show under MSAA",
                static_cast<int>(why.size()), why.data());
        return false;
    };
    static constexpr std::string_view kSamplers[] = {"s_src"};
    static constexpr rhi::VertexAttribute kAttributes[] = {{0, rhi::VertexFormat::Float2, 0}};
    const auto vsrc = rhi::embedded::gpu_feedback_quad_vert();
    const auto fsrc = rhi::embedded::gpu_ms_seed_frag();
    rhi::ShaderDesc vd;
    vd.stage = rhi::ShaderStage::Vertex;
    vd.glsl = vsrc.glsl;
    vd.spirv = vsrc.spirv;
    vd.name = "feedback_quad.vert";
    auto vs = device_.createShader(vd);
    if (!vs)
        return fail(vs.error());
    seed_vs_ = vs.value();
    rhi::ShaderDesc fd;
    fd.stage = rhi::ShaderStage::Fragment;
    fd.glsl = fsrc.glsl;
    fd.spirv = fsrc.spirv;
    fd.samplerNames = kSamplers;
    fd.name = "ms_seed.frag";
    auto fs = device_.createShader(fd);
    if (!fs)
        return fail(fs.error());
    seed_fs_ = fs.value();
    rhi::PipelineDesc p;
    p.vertexShader = seed_vs_;
    p.fragmentShader = seed_fs_;
    p.vertexLayout = {sizeof(float) * 2, kAttributes};
    p.name = "msaa seed";
    auto pipe = device_.createPipeline(p);
    if (!pipe)
        return fail(pipe.error());
    seed_pipe_ = pipe.value();
    rhi::BufferDesc b;
    b.usage = rhi::BufferUsage::Vertex;
    b.size = sizeof(float) * 6;
    b.name = "msaa seed triangle";
    auto tri = device_.createBuffer(b);
    if (!tri)
        return fail(tri.error());
    seed_triangle_ = tri.value();
    auto smp = device_.createSampler({});
    if (!smp)
        return fail(smp.error());
    seed_sampler_ = smp.value();
    return true;
}

void Targets::seed(rhi::CommandList& cmd)
{
    if (!seed_ready())
        return;
    static constexpr float kTriangle[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
    cmd.uploadBuffer(seed_triangle_, 0, std::as_bytes(std::span(kTriangle)));
    rhi::RenderPassDesc rp;
    rp.color = {ms_color_, rhi::LoadOp::DontCare, rhi::StoreOp::Store, {0.f, 0.f, 0.f, 1.f}};
    rp.name = "msaa seed";
    rhi::RenderPass* pass = cmd.beginRenderPass(rp);
    if (!pass)
        return;
    pass->bindPipeline(seed_pipe_);
    pass->setViewport({0.f, 0.f, static_cast<float>(width_), static_cast<float>(height_), 0.f, 1.f});
    pass->bindVertexBuffer(seed_triangle_, 0);
    pass->bindSampler(rhi::ShaderStage::Fragment, 0, scene_color_, seed_sampler_);
    pass->draw(3, 0);
    // The single-sample texture is a resolve target next: no sampler slot may keep it.
    pass->bindSampler(rhi::ShaderStage::Fragment, 0, {}, {});
    cmd.endRenderPass();
}

void Targets::resize(int window_w, int window_h)
{
    if (window_w <= 0 || window_h <= 0)
        return;
    present_w_ = window_w;
    present_h_ = window_h;
    applied_scale_ = view_.render_scale;

    // Native: the target keeps the display mode the game is in; a resize only moves where it lands.
    if (view_.native_render) {
        const int w = width_ > 0 ? width_ : 320;
        const int h = height_ > 0 ? height_ : 240;
        if (set_internal_resolution(w, h))
            return;
        view_.native_render = false;
    }

    // Borderless renders at the configured height, not the desktop size the window was created at. The width
    // follows the desktop's shape: at the configured shape a 16:9 size on a 3:2 desktop would be stretched in play
    // and lay the menus out for 16:9.
    if (view_.fullscreen_mode == 2 && view_.cfg_render_h > 0 && view_.cfg_render_h != window_h) {
        const int w = std::max(1, static_cast<int>((static_cast<long long>(view_.cfg_render_h) * window_w +
                                                    window_h / 2) / window_h));
        const Size s = scaled(w, view_.cfg_render_h);
        if (set_internal_resolution(s.w, s.h))
            return;
    }

    // The scene always draws into its own target: only the present touches the
    // swapchain.
    const Size s = scaled(window_w, window_h);
    set_internal_resolution(s.w, s.h);
}

void Targets::track_native(const DispEnv& disp)
{
    const int w = disp.disp.w, h = disp.disp.h;
    if (!view_.native_render || !scene_color_)
        return;
    if (w < 256 || w > 640 || h < 192 || h > 512)
        return;
    if (w == width_ && h == height_)
        return;
    set_internal_resolution(w, h);
}

bool Targets::ensure_offscreen(int w, int h)
{
    if (w <= 0 || h <= 0)
        return false;
    if (offscreen_color_ && offscreen_w_ == w && offscreen_h_ == h)
        return true;
    if (offscreen_color_)
        device_.destroy(offscreen_color_);
    offscreen_color_ = {};
    offscreen_w_ = offscreen_h_ = 0;

    rhi::TextureDesc desc;
    desc.format = rhi::TextureFormat::RGBA8;
    desc.width = static_cast<std::uint32_t>(w);
    desc.height = static_cast<std::uint32_t>(h);
    desc.usage = rhi::kUsageSampled | rhi::kUsageColorTarget;
    desc.name = "offscreen area";
    auto t = device_.createTexture(desc);
    if (!t)
        return false;
    offscreen_color_ = t.value();
    offscreen_w_ = w;
    offscreen_h_ = h;
    return true;
}

bool Targets::ui2d_wide() const
{
    // Every 2D screen spans the whole window (pictures stay at their aspect, centred); stretch mode
    // (menu_pillarbox 0) keeps stretching unless a screen asks for the width itself.
    return !view_.hor_plus && (view_.menu_pillarbox || view_.ui2d_wide_frames > 0) && ui_wide_factor() > 1.0f;
}

float Targets::ui_wide_factor() const { return pr::ui2d_wide_factor(view_.native_render, width_, height_); }

float Targets::live_pixel_aspect() const
{
    pr::PixelAspectInput in;
    in.raw = view_.aspect_raw;
    in.configured_par = view_.pixel_aspect;
    in.disp_w = static_cast<float>(disp_.disp.w);
    in.disp_h = static_cast<float>(disp_.disp.h);
    in.crt_trim = view_.crt_trim;
    in.hor_plus = view_.hor_plus;
    in.ui_pass = view_.ui_pass;
    in.item_take = view_.item_take;
    in.cutscene = view_.cutscene;
    in.world_hscale = view_.world_hscale;
    in.world_vscale = view_.world_vscale;
    in.cutscene_vscale = view_.cutscene_vscale;
    return pr::display_pixel_aspect(in);
}

namespace {

pr::ScreenOrthoInput ortho_input(const ViewSettings& v, const DispEnv& disp, int w, int h, bool wide, float factor,
                                 float par)
{
    pr::ScreenOrthoInput in;
    in.disp_w = static_cast<float>(disp.disp.w);
    in.disp_h = static_cast<float>(disp.disp.h);
    in.target_w = w;
    in.target_h = h;
    in.hor_plus = v.hor_plus;
    in.ui_pass = v.ui_pass;
    in.item_take = v.item_take;
    in.cutscene = v.cutscene;
    in.aspect_raw = v.aspect_raw;
    in.ui2d_wide = wide;
    in.widescreen_mode = v.widescreen_mode;
    in.ui2d_wide_factor = factor;
    in.world_hscale = v.world_hscale;
    in.world_vscale = v.world_vscale;
    in.cutscene_vscale = v.cutscene_vscale;
    in.vcrop_anchor = v.vcrop_anchor;
    in.pixel_aspect = par;
    return in;
}

pr::PictureRectInput picture_input(const ViewSettings& v, int w, int h, bool wide)
{
    pr::PictureRectInput in;
    in.target_w = w;
    in.target_h = h;
    in.native_render = v.native_render;
    in.hor_plus = v.hor_plus;
    in.widescreen_mode = v.widescreen_mode;
    in.menu_pillarbox = v.menu_pillarbox;
    in.ui2d_wide = wide;
    return in;
}

pr::PresentRectInput present_input(const ViewSettings& v, int pw, int ph, int w, int h)
{
    pr::PresentRectInput in;
    in.present_w = pw;
    in.present_h = ph;
    in.internal_w = w;
    in.internal_h = h;
    in.native_render = v.native_render;
    in.hor_plus = v.hor_plus;
    in.widescreen_mode = v.widescreen_mode;
    in.menu_pillarbox = v.menu_pillarbox;
    return in;
}

}  // namespace

bool Targets::hor_plus_half_widths(float* authored, float* installed) const
{
    if (!authored || !installed)
        return false;
    pr::HalfWidths hw;
    const auto in = ortho_input(view_, disp_, width_, height_, ui2d_wide(), ui_wide_factor(), live_pixel_aspect());
    if (!pr::hor_plus_half_widths(in, &hw))
        return false;
    *authored = hw.authored;
    *installed = hw.installed;
    return true;
}

DrawTarget Targets::for_split(const DrawEnv& env, const DispEnv& disp)
{
    disp_ = disp;
    DrawTarget t;

    if (!env.draw_on_display) {
        // The area is drawn on its own and later packed into video memory, so it gets a target of exactly its size
        // with the area's top-left at the target's top-left.
        const Rect area = env.clip;
        const bool same = offscreen_active_ && area == offscreen_area_;
        if (offscreen_active_ && !same) {
            t.offscreen_end = true;
            t.offscreen_writeback = offscreen_area_;
        }
        if (!same)
            t.offscreen_begin = true;
        offscreen_active_ = true;
        offscreen_area_ = area;
        ensure_offscreen(area.w, area.h);

        t.offscreen = true;
        t.color = offscreen_color_;
        t.viewport = viewport_of({0, 0, area.w, area.h});
        t.scissor_rect = {0, 0, area.w, area.h};
        t.projection = pr::ortho_matrix(0.0f, static_cast<float>(area.w), static_cast<float>(area.h), 0.0f, kNear,
                                        kFar);
        last_ = t;
        return t;
    }

    if (offscreen_active_) {
        t.offscreen_end = true;
        t.offscreen_writeback = offscreen_area_;
        offscreen_active_ = false;
    }
    t.color = draw_color();
    t.depth_stencil = draw_depth();
    t.scissor_rect = {0, 0, width_, height_};

    const bool wide = ui2d_wide();
    const pr::OrthoRect o =
        pr::screen_ortho(ortho_input(view_, disp, width_, height_, wide, ui_wide_factor(), live_pixel_aspect()));

    // The clip rect becomes a scissor in target pixels (computed bottom-left).
    // A scene redirect draws on the display in place of the held rect: the rect's corner stands for the display
    // buffer's, so what is drawn while the area is the rect keeps the whole picture (measured from the buffer's
    // corner the rect lies a screen away and its scissor would be empty).
    int clip_x = env.clip.x, clip_y = env.clip.y;
    if (scene_redirect_covers(clip_x, clip_y)) {
        clip_x += disp.disp.x - redirect_.x;
        clip_y += disp.disp.y - redirect_.y;
    }
    pr::ClipInput clip{clip_x, clip_y, env.clip.w, env.clip.h, disp.disp.x, disp.disp.y,
                       disp.disp.w, disp.disp.h, disp.interlaced, width_, height_};
    if (wide) {
        clip.wide = true;
        clip.ortho_left = o.left;
        clip.ortho_right = o.right;
    }
    pr::PlacedRect sc;
    bool scissor = pr::clip_scissor(clip, &sc);
    if (wide && !view_.ui_pass && view_.ui2d_wide_frames <= 0) {
        // A screen that did not lay itself out for the whole window keeps its world pass (backgrounds,
        // pictures) inside the authored frame: past its edges lies whatever video memory holds there.
        // Its UI pass (text, cursors, fades) still has the whole window.
        if (!scissor) sc = {0, 0, width_, height_};
        const pr::PlacedRect frame = pr::authored_frame_rect(o, static_cast<float>(disp.disp.w), width_, height_);
        sc = pr::intersect(sc, frame);
        scissor = true;
    }
    if (scissor) {
        t.scissor = true;
        t.scissor_rect = to_rhi(pr::flip_y(sc, height_));
    }
    t.projection = pr::ortho_matrix(o.left, o.right, o.bottom, o.top, kNear, kFar);

    const pr::PlacedRect pic = pr::picture_rect(picture_input(view_, width_, height_, wide));
    t.viewport = viewport_of(pr::flip_y(pic, height_));
    if (world_scale_ < 1.0f) {
        const rhi::Viewport full = t.viewport;
        t.viewport.w = std::max(1.0f, std::round(full.w * world_scale_));
        t.viewport.h = std::max(1.0f, std::round(full.h * world_scale_));
        const float sx = t.viewport.w / full.w, sy = t.viewport.h / full.h;
        if (t.scissor) {
            const rhi::Rect r = t.scissor_rect;
            const float x0 = std::floor(full.x + (static_cast<float>(r.x) - full.x) * sx);
            const float y0 = std::floor(full.y + (static_cast<float>(r.y) - full.y) * sy);
            const float x1 = std::ceil(full.x + (static_cast<float>(r.x + r.w) - full.x) * sx);
            const float y1 = std::ceil(full.y + (static_cast<float>(r.y + r.h) - full.y) * sy);
            t.scissor_rect = {static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                              static_cast<std::int32_t>(x1 - x0), static_cast<std::int32_t>(y1 - y0)};
        }
        world_full_ = full;
        world_small_ = t.viewport;
        world_scaled_ = true;
    }

    // Publish where the HUD edge and the world frame are, in prim coordinates, and where the display buffer lands
    // in the target, for the HUD placement, object picking and the feedback capture.
    const auto& fs = sh::frame::state();
    if (view_.hor_plus) {
        auto& dst = view_.ui_pass ? latches_.hud_rect : latches_.world_ortho_rect;
        dst = {o.left - fs.draw_offset_x, o.right - fs.draw_offset_x, o.top - fs.draw_offset_y,
               o.bottom - fs.draw_offset_y};
    }
    latches_.present_vp = {pic.x, pic.y, pic.w, pic.h};
    const float disp_w = static_cast<float>(disp.disp.w), disp_h = static_cast<float>(disp.disp.h);
    pr::AreaRect area;
    if (pr::display_area(o, disp_w, disp_h, pic, &area)) {
        if (view_.ui_pass) {
            latches_.ui_area_vp = {area.x0, area.y0, area.x1, area.y1};
            latches_.ui_area_vp_valid = true;
            latches_.feedback_wide_center = (o.left + o.right) * 0.5f;
            latches_.feedback_wide_scale = disp_w > 0.0f ? (o.right - o.left) / disp_w : 1.0f;
        } else {
            latches_.area_vp = {area.x0, area.y0, area.x1, area.y1};
            latches_.area_vp_valid = true;
            latches_.world_ortho = {o.left, o.right, o.top, o.bottom};
            latches_.world_disp = {disp_w, disp_h};
            latches_.world_ortho_valid = true;
        }
    }

    last_ = t;
    return t;
}

void Targets::set_world_scale(float scale)
{
    world_scale_ = std::clamp(scale, 0.1f, 1.0f);
    world_scaled_ = false;
}

bool Targets::world_scaled(rhi::Viewport* full, rhi::Viewport* scaled) const
{
    if (!world_scaled_)
        return false;
    *full = world_full_;
    *scaled = world_small_;
    return true;
}

void Targets::clear_depth(rhi::CommandList& cmd)
{
    // A whole clear still owed clears the depth too; otherwise this one waits like it (flush_clear).
    if (clear_pending_)
        return;
    depth_clear_pending_ = true;
}

void Targets::depth_clear_pass(rhi::CommandList& cmd)
{
    if (!draw_color() || !draw_depth())
        return;
    rhi::RenderPassDesc pass;
    pass.color.texture = draw_color();
    pass.color.load = rhi::LoadOp::Load;
    pass.color.store = rhi::StoreOp::Store;
    pass.depthStencil.texture = draw_depth();
    pass.depthStencil.load = rhi::LoadOp::Clear;
    pass.depthStencil.store = rhi::StoreOp::Store;
    pass.depthStencil.clearDepth = 1.0f;
    pass.depthStencil.clearStencil = 0;
    pass.name = "clear";
    if (cmd.beginRenderPass(pass))
        cmd.endRenderPass();
}

void Targets::clear_pass(rhi::CommandList& cmd, const std::array<float, 4>& colour)
{
    rhi::RenderPassDesc pass;
    pass.color.texture = draw_color();
    pass.color.load = rhi::LoadOp::Clear;
    pass.color.store = rhi::StoreOp::Store;
    pass.color.clear = colour;
    pass.depthStencil.texture = draw_depth();
    pass.depthStencil.load = rhi::LoadOp::Clear;
    pass.depthStencil.store = rhi::StoreOp::Store;
    pass.depthStencil.clearDepth = 1.0f;
    pass.depthStencil.clearStencil = 0;
    pass.name = "clear";
    if (cmd.beginRenderPass(pass))
        cmd.endRenderPass();
    // The whole target was cleared: the multisampled one holds the newest picture.
    ms_dirty_ = samples_ > 1;
    ms_stale_ = false;
}

void Targets::flush_clear(rhi::CommandList& cmd)
{
    if (depth_clear_pending_) {
        depth_clear_pending_ = false;
        depth_clear_pass(cmd);
    }
    if (clear_pending_) {
        clear_pending_ = false;
        if (scene_color_)
            clear_pass(cmd, pending_colour_);
    }
}

void Targets::clear(rhi::CommandList& cmd, const Rect& vram_rect, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    (void)vram_rect;
    // The whole picture is replaced: an atmosphere layer still owed has nothing left to go over.
    world_layer_ = {};
    if (!scene_color_)
        return;
    const std::array<float, 4> colour{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
    const pr::PlacedRect pic = pr::picture_rect(picture_input(view_, width_, height_, ui2d_wide()));
    const bool bars = pic.w != width_ || pic.h != height_;
    if (!bars) {
        depth_clear_pending_ = false;  // this clear takes the depth too
        // Recorded where the target is next drawn or read (flush_clear): the frame's first draws then begin with the
        // clear as their pass's load op, after the shadow maps of the batch, instead of in a pass of its own that a
        // tile-based GPU stores and loads again.
        clear_pending_ = true;
        pending_colour_ = colour;
        return;
    }
    // The bars stay black whatever the game clears to (the examine screen clears to its grey fog colour). Any clear
    // still owed is covered by this one.
    clear_pending_ = depth_clear_pending_ = false;
    clear_pass(cmd, std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
    if (pic.w <= 0 || pic.h <= 0)
        return;

    if (!solid_) {
        rhi::TextureDesc desc;
        desc.format = rhi::TextureFormat::RGBA8;
        desc.width = 1;
        desc.height = 1;
        desc.usage = rhi::kUsageSampled;
        desc.name = "clear colour";
        auto t = device_.createTexture(desc);
        if (!t)
            return;
        solid_ = t.value();
    }
    const std::array<std::byte, 4> texel{std::byte{r}, std::byte{g}, std::byte{b}, std::byte{0xFF}};
    cmd.uploadTexture(solid_, {0, 0, 1, 1}, texel);
    cmd.blitTexture(solid_, {0, 0, 1, 1}, resolved(cmd), to_rhi(pr::flip_y(pic, height_)), rhi::Filter::Nearest);
    wrote_resolved();
}

void Targets::picture_rect(int* x, int* y, int* w, int* h) const
{
    const pr::PlacedRect r = pr::flip_y(pr::picture_rect(picture_input(view_, width_, height_, ui2d_wide())), height_);
    if (x) *x = r.x;
    if (y) *y = r.y;
    if (w) *w = r.w;
    if (h) *h = r.h;
}

void Targets::present_rect(int* x, int* y, int* w, int* h) const
{
    const pr::PlacedRect r =
        pr::flip_y(pr::present_rect(present_input(view_, present_w_, present_h_, width_, height_)), present_h_);
    if (x) *x = r.x;
    if (y) *y = r.y;
    if (w) *w = r.w;
    if (h) *h = r.h;
}

void Targets::display_viewport(int* x, int* y, int* w, int* h) const
{
    const pr::PlacedRect r = overlay_to_window_
        ? pr::present_rect(present_input(view_, present_w_, present_h_, width_, height_))
        : pr::picture_rect(picture_input(view_, width_, height_, ui2d_wide()));
    if (x) *x = r.x;
    if (y) *y = r.y;
    if (w) *w = r.w;
    if (h) *h = r.h;
}

bool Targets::window_to_viewport(int mx, int my, float* fx, float* fy) const
{
    // The pointer arrives in window pixels; the picture rect is in scene-target pixels.
    if (scene_color_ && present_w_ > 0 && present_h_ > 0) {
        const pr::PlacedRect d = pr::present_rect(present_input(view_, present_w_, present_h_, width_, height_));
        if (d.w > 0 && d.h > 0) {
            mx = static_cast<int>(static_cast<float>(mx - d.x) * static_cast<float>(width_) / static_cast<float>(d.w));
            my = static_cast<int>(static_cast<float>(my - d.y) * static_cast<float>(height_) / static_cast<float>(d.h));
        }
    }
    const pr::PlacedRect vp = pr::picture_rect(picture_input(view_, width_, height_, ui2d_wide()));
    const float x = vp.w > 0 ? static_cast<float>(mx - vp.x) / static_cast<float>(vp.w) : 0.0f;
    const float y = vp.h > 0 ? static_cast<float>(my - vp.y) / static_cast<float>(vp.h) : 0.0f;
    if (fx) *fx = x;
    if (fy) *fy = y;
    return x >= 0.0f && x <= 1.0f && y >= 0.0f && y <= 1.0f;
}

bool Targets::set_scene_redirect(const Rect& rect)
{
    const bool armed = redirect_ttl_ == 0;
    redirect_ = rect;
    // Re-armed every frame the scene submits its area; a short lifetime lets it die out after the scene stops.
    redirect_ttl_ = 3;
    return armed;
}

bool Targets::scene_redirect_covers(int x, int y) const
{
    return redirect_ttl_ > 0 && x >= redirect_.x && x < redirect_.x + redirect_.w && y >= redirect_.y &&
           y < redirect_.y + redirect_.h;
}

bool Targets::scene_redirect(Rect* rect) const
{
    if (redirect_ttl_ <= 0)
        return false;
    if (rect)
        *rect = redirect_;
    return true;
}

bool Targets::tick_scene_redirect()
{
    if (redirect_ttl_ <= 0)
        return false;
    return --redirect_ttl_ == 0;
}

}  // namespace sh::gpu
