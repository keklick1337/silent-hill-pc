// SplitRenderer on the device: passes over the split targets, the scene point, capture splits, RHIDUMP. What each split is drawn with comes from split_plan.cpp.

#include "render/gpu/split_renderer.h"

#include "engine/log/sh_log.h"
#include "render/gpu/atmosphere_pass.h"
#include "render/gpu/temporal_pass.h"
#include "render/gpu/feedback_pass.h"
#include "render/gpu/page_resolve.h"
#include "render/gpu/renderer.h"
#include "render/gpu/shadow_pass.h"
#include "render/gpu/sun_shadow_pass.h"
#include "render/gpu/targets.h"
#include "render/gpu/textures.h"
#include "render/gpu/render_state.h"
#include "render/gpu/vram.h"
#include "render/rhi/rhi_vulkan_rt.h"
#include "render/scene/scene.h"
#include "render/shaderstate/shaderstate.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sh::gpu {

namespace {

void scene_log(int level, const std::string& message) {
    if (level != 0)
        SH_WARN("%s", message.c_str());
    else
        SH_LOG("%s", message.c_str());
}

bool same_target(const DrawTarget& a, rhi::TextureH color, rhi::TextureH depth) {
    return a.color == color && a.depth_stencil == depth;
}

// A scissor that equals "none": triangles are clipped to the viewport anyway, and a pass resets the scissor only
// at its start.
rhi::Rect viewport_rect(const rhi::Viewport& vp) {
    const int x0 = std::max(0, static_cast<int>(std::floor(vp.x)));
    const int y0 = std::max(0, static_cast<int>(std::floor(vp.y)));
    const int x1 = static_cast<int>(std::ceil(vp.x + vp.w));
    const int y1 = static_cast<int>(std::ceil(vp.y + vp.h));
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
}

}  // namespace

struct SplitRenderer::Pass {
    rhi::RenderPass* pass = nullptr;
    DrawTarget target;
    BoundTextures bound;
    int target_h = 0;  // height of the attached colour target (dither flip)
};

SplitRenderer::SplitRenderer(Renderer& owner) : owner_(owner), pass_(std::make_unique<Pass>()) {}

SplitRenderer::~SplitRenderer() = default;

namespace {
// The flashlight's lens radius in view units: scene.frag's kLightRadius, which sizes the PCSS penumbra.
constexpr float kRtLensRadius = 9.0f;
// Without fog the rays still end: about 128 m, farther than any room or street the game draws.
constexpr float kRtNoFogDistance = 32768.0f;

// The view distance at which the world's fog hides everything (the ramp at full strength): no secondary ray goes
// past it, so nothing shows that the fog hides.
float fog_opaque_distance(const scene::View& view) {
    const auto& p = view.fog.params;
    const float strength = view.fs.fog[3];
    if (p[0] < 0.5f || p[1] <= 0.0f || strength <= 0.0f) return kRtNoFogDistance;
    float far = 128.0f / p[1];
    for (int i = 0; i < 128; ++i) {
        const float amount =
            std::clamp(view.fog.ramp[static_cast<std::size_t>(i)] * 16.0f + p[2], 0.0f, 4096.0f) / 4096.0f * strength;
        if (amount >= 0.999f) {
            far = static_cast<float>(i) / p[1];
            break;
        }
    }
    if (p[3] > 0.0f) far = std::min(far, p[3]);
    return std::min(far, kRtNoFogDistance);
}

std::array<float, 3> to_view(const scene::Camera& c, const std::array<float, 3>& p, bool point) {
    std::array<float, 3> out{};
    for (std::size_t r = 0; r < 3; ++r) {
        double v = point ? c.trans[r] : 0.0;
        for (std::size_t k = 0; k < 3; ++k) v += c.rot[r * 3 + k] * static_cast<double>(p[k]);
        out[r] = static_cast<float>(v);
    }
    return out;
}

// The frame's map lights and sun in view space (rt_lights): only lights that can reach a point the fog leaves
// visible, the nearest first, at most kMaxRayLights. extra_only: just the lamps of extra_lights and the light that
// takes the flashlight's shadow map (frame light `shadowed`), without the sun (the map's other lights are off).
void describe_lights(const scene::Frame& frame, float fog_far, rhi::RayTracer::View* rv, bool extra_only = false,
                     std::int32_t shadowed = -1) {
    rv->mapLights.clear();
    for (std::size_t i = 0; i < frame.lights.size(); ++i) {
        const scene::FrameLight& l = frame.lights[i];
        if (extra_only && !l.extra && static_cast<std::int32_t>(i) != shadowed) continue;
        rhi::RayTracer::View::Light out;
        const std::array<float, 3> p = to_view(frame.camera, l.position, true);
        const float dist = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        if (dist - l.range > fog_far || l.range <= 0.0f) continue;
        const std::array<float, 3> d = to_view(frame.camera, l.direction, false);
        out.position = {p[0], p[1], p[2], l.range};
        out.direction = {d[0], d[1], d[2], l.cos_outer};
        out.color = {l.color[0], l.color[1], l.color[2], l.cos_inner};
        out.radius = l.radius;
        out.nearClip = l.near_clip;
        out.shadows = l.casts_shadows;
        out.gi = l.gi;
        rv->mapLights.push_back(out);
    }
    auto dist2 = [](const rhi::RayTracer::View::Light& l) {
        return l.position[0] * l.position[0] + l.position[1] * l.position[1] + l.position[2] * l.position[2];
    };
    std::sort(rv->mapLights.begin(), rv->mapLights.end(),
              [&](const auto& a, const auto& b) { return dist2(a) < dist2(b); });
    if (rv->mapLights.size() > rhi::kMaxRayLights) rv->mapLights.resize(rhi::kMaxRayLights);
    rv->sunDir = {0, 0, 0, 0};
    rv->sunColor = {0, 0, 0, 0};
    rv->sky = {0, 0, 0, 0};
    if (frame.sun.on && !extra_only) {
        const std::array<float, 3> d = to_view(frame.camera, frame.sun.direction, false);
        rv->sunDir = {d[0], d[1], d[2], 1.0f};
        rv->sunColor = {frame.sun.color[0], frame.sun.color[1], frame.sun.color[2], frame.sun.radius};
        rv->sky = {frame.sun.sky[0], frame.sun.sky[1], frame.sun.sky[2], 1.0f};
        rv->ceilings.clear();
        for (const std::array<float, 5>& c : frame.sun.ceilings) {
            if (rv->ceilings.size() / 3 >= rhi::kMaxRayCeilings) break;
            const std::array<float, 3> corner = to_view(frame.camera, {c[0], c[4], c[1]}, true);
            const std::array<float, 3> e1 = to_view(frame.camera, {c[2] - c[0], 0.0f, 0.0f}, false);
            const std::array<float, 3> e2 = to_view(frame.camera, {0.0f, 0.0f, c[3] - c[1]}, false);
            rv->ceilings.push_back({corner[0], corner[1], corner[2], 0.0f});
            rv->ceilings.push_back({e1[0], e1[1], e1[2], 0.0f});
            rv->ceilings.push_back({e2[0], e2[1], e2[2], 0.0f});
        }
    }
}

// A point light's shadow map spans this cone around its axis (a cosine; 70 degrees): one perspective map cannot
// take a whole sphere, and a held flame's shadows that matter (the body, the ground and walls behind it) lie in it.
constexpr float kPointLightShadowCos = 0.342f;

// How much of a light a flake holds back (particle_shadows): snow and ash are thin, so their shadows stay faint.
constexpr float kFlakeOpacity = 0.55f;
// Without the flashlight the flakes nearest this view point cast (about where the camera looks at Harry).
constexpr float kFlakeAnchorZ = 768.0f;

// The map's lights, the sun with its shadow map and the falling flakes for the scene shader without ray tracing
// (map_lights, sun_shadows, particle_shadows; RENDER.md 7.14). The ray-traced light replaces the lights and the sun
// where it is on; the flakes shade both paths.
// The lamps of extra_lights need none of the native Vulkan passes: they shade on every backend, also where
// map_lights is off or unavailable (atmosphere: whether the native passes run).
void describe_scene_lights(const scene::Frame& frame, float fog_far, bool traced, bool atmosphere,
                           const SunShadowPass& sun, std::int32_t shadowed, scene::View* view) {
    const render::AtmosphereSettings& s = render::atmosphere();
    scene::LightBlock& b = view->lights;
    b = {};
    const bool all = atmosphere && s.map_lights != 0;
    if ((all || render::graphics().extra_lights != 0) && !traced) {
        rhi::RayTracer::View lights;
        describe_lights(frame, fog_far, &lights, !all, shadowed);
        // The same transform gives the same numbers: the block's entry of the light the shadow map belongs to.
        std::array<float, 3> shadowed_at{};
        if (shadowed >= 0)
            shadowed_at = to_view(frame.camera, frame.lights[static_cast<std::size_t>(shadowed)].position, true);
        std::size_t n = 0;
        for (const rhi::RayTracer::View::Light& l : lights.mapLights) {
            if (n >= scene::kSceneLights) break;
            if (shadowed >= 0 && b.shadow[0] == 0.0f && l.position[0] == shadowed_at[0] &&
                l.position[1] == shadowed_at[1] && l.position[2] == shadowed_at[2])
                b.shadow[0] = static_cast<float>(n + 1);
            b.pos[n] = l.position;
            b.dir[n] = l.direction;
            b.color[n] = l.color;
            ++n;
        }
        b.info[0] = static_cast<float>(n);
        if (lights.sunDir[3] > 0.5f) {
            b.info[1] = 1.0f;
            b.sun_dir = {lights.sunDir[0], lights.sunDir[1], lights.sunDir[2], 0.0f};
            b.sun_color = lights.sunColor;
            if (s.sun_shadows != 0 && sun.ready(frame.serial)) {
                b.info[2] = 1.0f;
                b.sun_dir[3] = sun.fit().texel;
                b.sun_matrix = sun.fit().receiver;
                view->sun_shadow = sun.map();
                view->sun_sampler = sun.sampler();
            }
        }
    }
    if (!atmosphere) return;
    if (s.particle_shadows != 0 && !frame.flakes.empty()) {
        const bool beam = view->fs.flMode[0] > 0.5f;
        const std::array<float, 3> at =
            beam ? std::array<float, 3>{view->fs.flPos[0], view->fs.flPos[1], view->fs.flPos[2]}
                 : std::array<float, 3>{0.0f, 0.0f, kFlakeAnchorZ};
        std::vector<std::pair<float, std::size_t>> near;
        near.reserve(frame.flakes.size());
        for (std::size_t i = 0; i < frame.flakes.size(); ++i) {
            const auto& f = frame.flakes[i];
            const float dx = f[0] - at[0], dy = f[1] - at[1], dz = f[2] - at[2];
            near.emplace_back(dx * dx + dy * dy + dz * dz, i);
        }
        const std::size_t n = std::min(near.size(), scene::kSceneFlakes);
        std::partial_sort(near.begin(), near.begin() + static_cast<std::ptrdiff_t>(n), near.end());
        for (std::size_t i = 0; i < n; ++i) b.flakes[i] = frame.flakes[near[i].second];
        b.info[3] = static_cast<float>(n);
        b.sun_map[2] = kFlakeOpacity;
        b.sun_map[3] = 1.0f;
    }
    b.sun_map[0] = static_cast<float>(sun.side());
    b.sun_map[1] = sun.fit().depth_range;
}
}  // namespace

void SplitRenderer::trace_frame(rhi::CommandList& cmd, const scene::View& view) {
    scene::Frame& frame = scene::current_frame();
    rt_serial_ = frame.serial;
    rt_serial_valid_ = true;
    rt_frame_ = {};
    if (!rt_ && !rt_failed_) {
        auto r = rhi::RayTracer::create(owner_.device());
        if (!r) {
            rt_failed_ = true;
            SH_WARN("[RT] %s", r.error().c_str());
            return;
        }
        rt_ = std::move(r.value());
        rt_scene_ = std::make_unique<rhi::RayScene>();
        SH_LOG("[RT] ray tracing on: acceleration structures of the scene per frame");
    }
    if (!rt_ || !scene_) return;
    close_pass(cmd);
    scene_->ray_scene(frame, rt_scene_.get());
    if (auto r = rt_->build(*rt_scene_); !r) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[RT] build: %s", r.error().c_str());
        return;
    }
    const render::GraphicsSettings& g = render::graphics();
    rhi::RayTracer::View rv;
    rv.shadows = g.rt_shadows != 0;
    rv.reflections = g.rt_reflections != 0;
    rv.gi = g.rt_gi != 0;
    rv.lights = g.rt_lights != 0;
    if (rv.shadows || rv.reflections || rv.gi || rv.lights) {
        const DrawTarget& t = pass_->target;
        rv.target = t.color;
        rv.viewport = {t.viewport.x, t.viewport.y, t.viewport.w, t.viewport.h};
        // Pixel centre to view ray, the inverse of scene.vert's projection: screen = H * xy / z + centre, the
        // ortho matrix to NDC, NDC to the viewport with +Y up (the backend flips the viewport).
        const auto& o = view.ortho;
        const float hp = frame.h > 0.0f ? frame.h : 1.0f;
        const float ox = frame.geom_ofs_x + view.draw_ofs_x, oy = frame.geom_ofs_y + view.draw_ofs_y;
        const float vx = t.viewport.x, vy = t.viewport.y, vw = std::max(t.viewport.w, 1.0f),
                    vh = std::max(t.viewport.h, 1.0f);
        rv.pixelRay = {2.0f / (vw * o[0] * hp), ((-2.0f * vx / vw - 1.0f - o[12]) / o[0] - ox) / hp,
                       -2.0f / (vh * o[5] * hp), ((1.0f + 2.0f * vy / vh - o[13]) / o[5] - oy) / hp};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c)
                rv.camera[static_cast<std::size_t>(r * 4 + c)] = frame.camera.rot[static_cast<std::size_t>(r * 3 + c)];
            rv.camera[static_cast<std::size_t>(r * 4 + 3)] = frame.camera.trans[static_cast<std::size_t>(r)];
        }
        const auto& fs = view.fs;
        const bool classic = fs.flMode[1] > 0.5f;
        float dx = fs.flDir[0], dy = fs.flDir[1], dz = fs.flDir[2];
        const float dl = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dl > 1e-6f) {
            dx /= dl;
            dy /= dl;
            dz /= dl;
        } else {
            dx = dy = 0.0f;
            dz = 1.0f;
        }
        // scene.frag's light origin: the Classic beam starts behind the lens.
        const float back = classic ? 39.0f : 0.0f;
        rv.lens = {fs.flPos[0] - dx * back, fs.flPos[1] - dy * back, fs.flPos[2] - dz * back, kRtLensRadius};
        rv.beamDir = {dx, dy, dz, fs.flDir[3]};
        rv.beam = {fs.flPos[3], fs.flMode[3] * (classic ? 0.5f : 1.0f), classic ? 1.0f : 0.0f,
                   fs.flMode[0] > 0.5f ? 1.0f : 0.0f};
        rv.beamColor = fs.flColor;
        rv.fog = fs.fog;
        rv.fogParams = view.fog.params;
        rv.fogRamp = view.fog.ramp;
        // The shadow map's near plane keeps the hand holding the torch out of its own beam; the rays stop there too.
        rv.nearClip = fs.prog[3] > 0.5f ? std::max(fs.shadowQ[2], 8.0f) : 24.0f;
        // What a cell the rays find is lit with besides the beam: the map's flat tint, or about half of it for the
        // point and directional lights the vertex shader evaluates per vertex.
        const scene::Lighting& light = frame.light;
        const float lit = light.mode == 0 ? 1.0f / 128.0f : 0.5f / 128.0f;
        rv.ambient = {light.tint[0] * lit, light.tint[1] * lit, light.tint[2] * lit, fog_opaque_distance(view)};
        if (rv.lights) describe_lights(frame, rv.ambient[3], &rv);
        // The world's up (PS1 y points down) in view space: the camera's rotation of (0, -1, 0).
        rv.up = {static_cast<float>(-frame.camera.rot[1]), static_cast<float>(-frame.camera.rot[4]),
                 static_cast<float>(-frame.camera.rot[7]), 0.0f};
        auto r = rt_->trace(rv);
        if (!r) {
            static int s_logs = 0;
            if (s_logs++ < 4) SH_WARN("[RT] trace: %s", r.error().c_str());
        } else {
            rt_frame_.shadow = r.value().shadow;
            rt_frame_.reflect = r.value().reflect;
            rt_frame_.gi = r.value().gi;
            rt_frame_.reflect_mirrors = rv.reflections;
            rt_frame_.target = t.color;
            rt_frame_.viewport = rv.viewport;
            rt_frame_.ortho = view.ortho;
            rt_frame_.draw_ofs_x = view.draw_ofs_x;
            rt_frame_.draw_ofs_y = view.draw_ofs_y;
        }
    }
    static std::uint64_t s_frames = 0;
    if (s_frames++ % 600 == 0) {
        const rhi::RayTracer::Stats& s = rt_->stats();
        SH_LOG(
            "[RT] %u instances, %llu triangles; %u kept BLAS (%u compacted, %llu KB), TLAS %llu KB, scratch %llu KB; "
            "built %u, waiting %u",
            s.instances, static_cast<unsigned long long>(s.triangles), s.blas, s.compacted,
            static_cast<unsigned long long>(s.blasBytes >> 10), static_cast<unsigned long long>(s.tlasBytes >> 10),
            static_cast<unsigned long long>(s.scratchBytes >> 10), s.built, s.pending);
    }
}

void SplitRenderer::upload(rhi::CommandList& cmd, const SplitBatch& batch) {
    uploaded_ = nullptr;
    if (batch.vertices.empty()) return;
    auto r = owner_.prims().uploadVertices(cmd, batch.vertices);
    if (!r) {
        stats_.failures++;
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[GPU] vertex upload: %s", r.error().c_str());
        return;
    }
    uploaded_ = &batch;
    uploaded_count_ = batch.vertices.size();
}

SplitContext SplitRenderer::context(const SplitBatch& batch, bool item_pass) const {
    SplitContext ctx;
    ctx.projection = pass_->target.projection;
    ctx.item_pass = item_pass;
    ctx.multisampled = owner_.targets().samples() > 1 && pass_->target.color == owner_.targets().draw_color();
    ctx.force_item_depth = batch.force_item_depth;
    ctx.shadow_map = static_cast<bool>(owner_.shadow().map());
    ctx.target_height = owner_.device().caps().fragCoordTopLeft ? static_cast<float>(pass_->target_h) : 0.f;
    return ctx;
}

void SplitRenderer::open_pass(rhi::CommandList& cmd, const Split& split) {
    Pass& p = *pass_;
    DrawTarget t = owner_.targets().for_split(split.env, split.disp);
    // Leaving an offscreen area: its picture goes into video memory before anything draws on (the redirect target
    // is the same texture for the next area of the same size).
    if (t.offscreen_end) {
        close_pass(cmd);
        owner_.feedback().write_back_offscreen(cmd, t.offscreen_writeback);
    }
    if (!p.pass || t.offscreen_begin || !same_target(p.target, t.color, t.depth_stencil)) {
        close_pass(cmd);
        if (t.color && t.color == owner_.targets().draw_color()) owner_.targets().begin_draw(cmd);
        rhi::RenderPassDesc desc;
        desc.color = {t.color, t.offscreen_begin ? rhi::LoadOp::Clear : rhi::LoadOp::Load, rhi::StoreOp::Store,
                      t.offscreen_begin ? t.offscreen_clear : std::array<float, 4>{0.f, 0.f, 0.f, 1.f}};
        desc.depthStencil = {t.depth_stencil, rhi::LoadOp::Load, rhi::StoreOp::Store, 1.f, 0};
        desc.name = "scene";
        p.pass = cmd.beginRenderPass(desc);
        p.bound.reset();
        if (p.pass) {
            owner_.prims().resetBindings();
            owner_.prims().beginDraws(*p.pass, rhi::PrimVsUniforms{}, rhi::PrimFsUniforms{});
            // beginDraws bound VRAM to every slot.
            p.bound.vram = p.bound.rgba = p.bound.shadow = owner_.prims().vram();
        }
        p.target_h = t.color == owner_.targets().draw_color()
                         ? owner_.targets().height()
                         : static_cast<int>(t.viewport.y + t.viewport.h);
    }
    p.target = t;
    if (!p.pass) return;
    p.pass->setViewport(t.viewport);
    p.pass->setScissor(t.scissor ? t.scissor_rect : viewport_rect(t.viewport));
}

void SplitRenderer::close_pass(rhi::CommandList& cmd) {
    if (!pass_->pass) return;
    cmd.endRenderPass();
    pass_->pass = nullptr;
    pass_->bound.reset();
}

void SplitRenderer::draw_prims(rhi::CommandList& cmd, const Split& split, const SplitBatch& batch,
                               const DrawStep& step) {
    open_pass(cmd, split);
    Pass& p = *pass_;
    if (!p.pass) return;
    Split s = split;
    s.first = step.first;
    s.count = step.count;
    SplitDraw d = plan_split(s, batch.vertices, shade_, context(batch, step.item_pass));
    if (d.skip) return;
    if (d.shadow) owner_.shadow().shader_inputs(&d.fs);
    if (d.rgba) owner_.feedback().shader_inputs(&d.fs, split.override_info.feedback_screen);

    SplitBindings b;
    b.vram = owner_.prims().vram();
    b.vram_sampler = owner_.prims().vramSampler();
    if (d.rgba) {
        b.rgba = owner_.textures().resolve(split.texture);
        if (!b.rgba) b.rgba = owner_.textures().white();
        b.rgba_sampler = owner_.textures().sampler(split.texture.id);
    }
    if (d.shadow) {
        b.shadow = owner_.shadow().map();
        b.shadow_sampler = owner_.shadow().sampler();
    }

    auto r = issue_split(*p.pass, owner_.prims(), d, b, p.bound);
    stats_.draws++;
    if (!r) {
        stats_.failures++;
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[GPU] split draw: %s", r.error().c_str());
        return;
    }
}

scene::SceneRenderer* SplitRenderer::scene_renderer() {
    if (scene_ || scene_failed_) return scene_.get();
    auto r = scene::SceneRenderer::create(owner_.device(), rhi::TextureFormat::RGBA8);
    if (!r) {
        scene_failed_ = true;
        SH_WARN("[SCENE] renderer: %s; the 3D scene is not drawn", r.error().c_str());
        return nullptr;
    }
    scene_ = std::move(r.value());
    SH_LOG("[SCENE] the 3D scene is drawn by the engine (%s)", owner_.device().caps().driver.c_str());
    return scene_.get();
}

// The PS1 frame state at the scene point: the split's ortho and viewport, the fog and
// flashlight of an opaque 16-bit prim there.
void SplitRenderer::draw_scene(rhi::CommandList& cmd, const Split& split) {
    scene::Frame& frame = scene::current_frame();
    if (split.scene_item < 0 && !frame.world) return;
    scene::set_log(scene_log);
    scene::SceneRenderer* sr = scene_renderer();
    if (!sr) return;
    if (!scene_prepared_valid_ || scene_prepared_ != frame.serial) close_pass(cmd);
    if (!prepare_scene(cmd)) return;

    open_pass(cmd, split);
    Pass& p = *pass_;
    if (!p.pass) return;
    Split probe;
    probe.env = split.env;
    probe.disp = split.disp;
    probe.mask_mode = split.mask_mode;
    probe.count = 3;
    static const Vertex kFlat[3]{};
    SplitBatch none;
    const SplitDraw d = plan_split(probe, kFlat, shade_, context(none, false));

    scene::View view;
    view.ortho = d.vs.projection;
    if (!p.target.offscreen && p.target.color == owner_.targets().draw_color())
        owner_.temporal().jitter(view, p.target.viewport.w, p.target.viewport.h);
    view.sz_max = d.vs.pgxp[1] > 1.0f ? d.vs.pgxp[1] : 65535.0f;
    view.draw_ofs_x = split.scene_ofs_x;
    view.draw_ofs_y = split.scene_ofs_y;
    view.pixel_w = p.target.viewport.w;
    view.pixel_h = p.target.viewport.h;
    view.fs = d.fs;
    view.fog = d.fog;
    view.fog_ramp = owner_.prims().fogRamp();
    view.fs.modes[0] = 0.0f;
    view.fs.rgbaFb[3] = 0.0f;  // the scene has its own cutout; foliage_edge is a PS1-prim input
    // The flashlight's map, or a map light's (it is off then): the scene shader shades that light through it.
    if (d.shadow || owner_.shadow().shadowed_light() >= 0) {
        owner_.shadow().shader_inputs(&view.fs);
        view.shadow_map = owner_.shadow().map();
        view.shadow_sampler = owner_.shadow().sampler();
        view.shadow_tint = owner_.shadow().tint();
    } else {
        view.fs.prog[3] = 0.0f;
    }
    // The water's mirrored picture is a pass of its own, drawn right before the cells that show it.
    if (split.scene_item < 0) {
        close_pass(cmd);
        sr->reflect(cmd, frame, view);
        open_pass(cmd, split);
        if (!pass_->pass) return;
    }
    // Ray tracing once per frame, at its first scene draw into the scene target, with this draw's view.
    if (frame.world && render::graphics().ray_tracing && !rt_failed_ &&
        (!rt_serial_valid_ || rt_serial_ != frame.serial) && pass_->target.color == owner_.targets().draw_color()) {
        trace_frame(cmd, view);
        open_pass(cmd, split);
        if (!pass_->pass) return;
    }
    if (rt_serial_valid_ && rt_serial_ == frame.serial && rt_frame_.target == pass_->target.color &&
        rt_frame_.ortho == view.ortho && rt_frame_.draw_ofs_x == view.draw_ofs_x &&
        rt_frame_.draw_ofs_y == view.draw_ofs_y &&
        rt_frame_.viewport == std::array<float, 4>{pass_->target.viewport.x, pass_->target.viewport.y,
                                                   pass_->target.viewport.w, pass_->target.viewport.h}) {
        view.rt_shadow = rt_frame_.shadow;
        view.rt_reflect = rt_frame_.reflect;
        view.rt_reflect_mirrors = rt_frame_.reflect_mirrors;
        view.rt_gi = rt_frame_.gi;
    }
    {
        const render::GraphicsSettings& g = render::graphics();
        const bool traced = g.ray_tracing != 0 && g.rt_lights != 0 && view.rt_gi;
        describe_scene_lights(frame, fog_opaque_distance(view), traced, owner_.atmosphere().available(),
                              owner_.sun_shadow(), traced ? -1 : owner_.shadow().shadowed_light(), &view);
    }
    auto r = sr->draw(*pass_->pass, frame, view, split.scene_item);
    if (split.scene_item < 0 && !p.target.offscreen && p.target.color == owner_.targets().draw_color())
        note_world(frame, view);
    if (std::getenv("SH_GPU_SCENE_DBG") && stats_.scenes % 200 == 0)
        SH_LOG("[SCENEDBG] item %d off %d vp %.0f %.0f %.0f %.0f szmax %.0f ortho %.4f %.4f %.4f %.4f ofs %.1f %.1f cells %zu",
               split.scene_item, p.target.offscreen ? 1 : 0, p.target.viewport.x, p.target.viewport.y,
               p.target.viewport.w, p.target.viewport.h, view.sz_max, view.ortho[0], view.ortho[5], view.ortho[12],
               view.ortho[13], view.draw_ofs_x, view.draw_ofs_y, frame.cells.size());
    stats_.scenes++;
    if (!r) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[SCENE] draw: %s", r.error().c_str());
    }
    // The scene bound its own pipeline, buffers and uniforms.
    owner_.prims().resetBindings();
    owner_.prims().beginDraws(*p.pass, rhi::PrimVsUniforms{}, rhi::PrimFsUniforms{});
    p.bound.vram = p.bound.rgba = p.bound.shadow = owner_.prims().vram();
}

// What the atmosphere pass needs of the world's scene draw: the target and the projection it was drawn with.
void SplitRenderer::note_world(const scene::Frame& frame, const scene::View& view) {
    const Pass& p = *pass_;
    rhi::AtmosphereInputs in;
    in.color = p.target.color;
    in.depth = p.target.depth_stencil;
    in.viewport_x = p.target.viewport.x;
    in.viewport_y = p.target.viewport.y;
    in.viewport_w = p.target.viewport.w;
    in.viewport_h = p.target.viewport.h;
    const float cx = frame.geom_ofs_x + view.draw_ofs_x, cy = frame.geom_ofs_y + view.draw_ofs_y;
    in.proj = {view.ortho[0] * frame.h, view.ortho[5] * frame.h, view.ortho[0] * cx + view.ortho[12],
               view.ortho[5] * cy + view.ortho[13]};
    in.sz_max = view.sz_max;
    in.fs = view.fs;
    in.fog = view.fog;
    if (view.fs.prog[3] > 0.5f) in.shadow_map = view.shadow_map;
    const render::GraphicsSettings& g = render::graphics();
    const render::AtmosphereSettings& a = render::atmosphere();
    const bool all_lights = (g.ray_tracing != 0 && g.rt_lights != 0) || a.map_lights != 0;
    if (all_lights || g.extra_lights != 0) {
        rhi::RayTracer::View lights;
        describe_lights(frame, fog_opaque_distance(view), &lights, !all_lights);
        for (const rhi::RayTracer::View::Light& l : lights.mapLights) {
            if (in.lights >= rhi::kAtmosphereLights) break;
            in.lightPos[static_cast<std::size_t>(in.lights)] = l.position;
            in.lightColor[static_cast<std::size_t>(in.lights)] = {l.color[0], l.color[1], l.color[2], 0.0f};
            ++in.lights;
        }
    }
    // God rays: the sun scatters in the fog where its shadow map sees it.
    const SunShadowPass& sun = owner_.sun_shadow();
    if (a.god_rays != 0 && frame.sun.on && sun.ready(frame.serial)) {
        const std::array<float, 3> d = to_view(frame.camera, frame.sun.direction, false);
        in.sun = true;
        in.sun_map = sun.map();
        in.sunDir = {d[0], d[1], d[2], sun.fit().depth_range};
        in.sunColor = {frame.sun.color[0], frame.sun.color[1], frame.sun.color[2], 0.0f};
        in.sunMatrix = sun.fit().receiver;
    }
    owner_.atmosphere().note_world(in);
    owner_.temporal().note_world(frame, view, p.target.color, p.target.depth_stencil, p.target.viewport);
}

bool SplitRenderer::prepare_scene(rhi::CommandList& cmd) {
    scene::Frame& frame = scene::current_frame();
    scene::SceneRenderer* sr = scene_renderer();
    if (!sr) return false;
    if (scene_prepared_valid_ && scene_prepared_ == frame.serial) return true;
    const Result<void> prep = sr->prepare(cmd, frame);
    scene_prepared_ = frame.serial;
    scene_prepared_valid_ = true;
    if (!prep) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[SCENE] frame: %s", prep.error().c_str());
        scene_prepared_valid_ = false;
        return false;
    }
    return true;
}

std::optional<LightShadow> SplitRenderer::map_light_shadow() {
    const render::GraphicsSettings& g = render::graphics();
    if (g.ray_tracing != 0 && g.rt_lights != 0 && !rt_failed_) return std::nullopt;
    // Only where the light is shaded at all: with map_lights, or with the extra lamps where those cannot run.
    if (!(owner_.atmosphere().available() && render::atmosphere().map_lights != 0) && g.extra_lights == 0)
        return std::nullopt;
    const scene::Frame& frame = scene::current_frame();
    if (!frame.world) return std::nullopt;
    for (std::size_t i = 0; i < frame.lights.size(); ++i) {
        const scene::FrameLight& l = frame.lights[i];
        if (!l.shadow_map || l.range <= 0.0f) continue;
        const bool spot = l.direction[0] != 0.0f || l.direction[1] != 0.0f || l.direction[2] != 0.0f;
        LightShadow s;
        s.position = to_view(frame.camera, l.position, true);
        s.axis = to_view(frame.camera, l.shadow_axis, false);
        s.cos_half = spot ? l.cos_outer : kPointLightShadowCos;
        s.range = l.range;
        s.near_clip = l.near_clip;
        s.frame_light = static_cast<std::int32_t>(i);
        return s;
    }
    return std::nullopt;
}

void SplitRenderer::draw_scene_casters(rhi::RenderPass& pass, std::int32_t item,
                                       const std::array<float, 16>& light_matrix, float z_far, bool tint) {
    scene::Frame& frame = scene::current_frame();
    if ((item < 0 && !frame.world) || !scene_) return;
    scene::View view;
    view.fs.shadowMatrix = light_matrix;
    view.fs.shadowQ[3] = z_far;
    auto r = scene_->draw(pass, frame, view, item, tint ? scene::SceneRenderer::Layer::ShadowTint : scene::SceneRenderer::Layer::ShadowDepth);
    if (!r) {
        static int s_logs = 0;
        if (s_logs++ < 4) SH_WARN("[SCENE] casters: %s", r.error().c_str());
    }
}

void SplitRenderer::draw(rhi::CommandList& cmd, SplitBatch& batch) {
    const auto& ramp = shaderstate::fogRamp();
    std::memcpy(shade_.fog.params.data(), ramp.params(), sizeof(float) * shade_.fog.params.size());
    std::memcpy(shade_.fog.ramp.data(), ramp.ramp(), sizeof(float) * shade_.fog.ramp.size());
    owner_.prims().uploadFogRamp(cmd, shade_.fog);

    if (uploaded_ != &batch || uploaded_count_ != batch.vertices.size()) upload(cmd, batch);
    const bool have_vertices = uploaded_ == &batch || batch.vertices.empty();
    uploaded_ = nullptr;

    for (const DrawStep& step : order_splits(batch)) {
        const Split& split = batch.splits[step.split];
        stats_.splits++;
        switch (split.kind) {
            case Split::Kind::Prims:
                if (have_vertices) draw_prims(cmd, split, batch, step);
                break;
            case Split::Kind::Scene:
                draw_scene(cmd, split);
                break;
            case Split::Kind::Capture:
                close_pass(cmd);
                owner_.feedback().capture_to_vram(cmd, split.capture);
                break;
        }
    }
    close_pass(cmd);
}

}  // namespace sh::gpu
