// Draws one SplitBatch (RENDER.md section 3): uploads the vertex stream, then per split the pipeline key
// (blend, depth, stencil mask mode), the target (Targets::for_split), the texture, the uniform blocks (fog, flashlight,
// dither, override and feedback inputs, src/render/shaderstate) and the draw through rhi::PrimRenderer; the
// isolated-item pass (opaque first, translucent triangles back to front); Scene splits to the scene renderer, Capture
// splits to FeedbackPass.
//
// The pure half (ShadeInputs, plan_split, order_splits, issue_split) is in split_plan.cpp so it links without a
// device; split_renderer.cpp drives passes on the real one.
#pragma once

#include "render/gpu/gpu_types.h"
#include "render/rhi/prim_batch.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sh::scene {
class SceneRenderer;
struct Frame;
struct View;
}

namespace sh::rhi {
class RayTracer;
struct RayScene;
}  // namespace sh::rhi

namespace sh::gpu {

struct LightShadow;

class Renderer;

// Frame-wide settings the splits are shaded with: the config keys, presets and per-frame game state.
// The game's render bridge fills them before each flush; defaults are the config
// defaults.
struct ShadeInputs {
    // Depth (PGXP)
    bool pgxp = false;
    float sz_max = 65536.f;            // the depth the per-corner depths are normalised by
    float far_w_clamp = 0.f;           // render::precise().far_w_clamp
    float world_far_bias = 0.f;        // render::precise().world_far_bias
    bool pgxp_zbuffer = true;          // render::graphics().depth_buffer: a depth request only takes effect with it

    // Fog
    bool soft_fog = false;
    std::array<float, 3> fog_color{0.f, 0.f, 0.f};
    float fog_strength = 1.1f;

    // Filtering, dither
    bool native_render = false;        // render::picture().native_render: point sampling, no sharp filter
    bool dither_suppressed = false;    // render::frame_state().dither_suppressed: a 2D frame (menus)
    int texture_filter = 1;            // render::graphics().texture_filter
    int aniso_level = 1;               // render::graphics().aniso_level
    bool menu_filter = false;          // render::graphics().menu_filter
    bool dither = false;
    bool deband = false;               // render::graphics().deband
    int window_width = 640;            // render::published().scene_width (dither cell)
    int foliage_edge = 0;              // render::graphics().foliage_aa
    bool multisampled = false;         // the scene target has samples (foliage_edge 2; SplitContext says per pass)

    // Flashlight
    bool per_pixel_flashlight = false;
    bool flashlight_active = false;
    bool flashlight_fps_mode = false;
    bool flashlight_style = false;     // false classic, true modern
    bool soft_highlights = false;
    float flashlight_intensity = 1.f, flashlight_intensity_fps = 1.f;
    float flashlight_size = 1.f, flashlight_size_fps = 1.f;
    float flashlight_inner_cos = 0.95f, flashlight_outer_cos = 0.85f;
    float flashlight_range = 4096.f;
    std::array<float, 3> flashlight_pos{0.f, 0.f, 0.f};
    std::array<float, 3> flashlight_dir{0.f, 0.f, 1.f};
    std::array<float, 3> flashlight_color{1.f, 1.f, 1.f};
    // Shadow sampling wanted (flashlight shadows on, per-pixel light, light on, not a re-presented frame); the map
    // itself is ShadowPass's.
    bool shadows = false;

    float feedback_damp = 1.f;         // render::feedback_control().damp_blend: ConstantAlpha splits

    // World fog ramp (sh::shaderstate::fogRamp()); SplitRenderer refreshes it on every draw.
    rhi::FogRampUniforms fog;
};

// What the pass around a split provides.
struct SplitContext {
    std::array<float, 16> projection{};  // DrawTarget::projection
    bool item_pass = false;              // inside the isolated-item bracket of a force_item_depth batch
    bool force_item_depth = false;       // SplitBatch::force_item_depth
    bool shadow_map = false;             // a shadow map exists to sample
    float target_height = 0.f;           // backends with a top-left gl_FragCoord: the dither phase flip
    bool multisampled = false;           // the pass draws into the multisampled scene target
};

// One Prims split as prim_batch draws it.
struct SplitDraw {
    bool skip = false;  // nothing to draw (empty, or dropped by the debug add mode)
    rhi::PrimPipelineKey key;
    rhi::PrimVsUniforms vs;
    rhi::PrimFsUniforms fs;
    rhi::FogRampUniforms fog;
    bool rgba = false;     // the RGBA override program (s_rgba)
    bool shadow = false;   // samples the shadow map (s_shadow)
    float damp = 1.f;      // blend constant of ConstantAlpha
    std::uint32_t first = 0, count = 0;
};

// The state key and uniform blocks of `split`. Shadow matrix and feedback
// terms are left to ShadowPass::shader_inputs / FeedbackPass::shader_inputs (fs.prog[3] / the split's code say when).
SplitDraw plan_split(const Split& split, std::span<const Vertex> vertices, const ShadeInputs& in,
                     const SplitContext& ctx);

// The draw order of DrawAllSplits: every split once, except that in a force_item_depth batch a run of opaque and
// average-blended Prims splits with translucency is drawn as its opaque splits first, then its translucent triangles
// back to front (one step per triangle).
struct DrawStep {
    std::uint32_t split = 0;
    std::uint32_t first = 0, count = 0;  // the vertex range (a triangle for a sorted translucent step)
    bool item_pass = false;
};
std::vector<DrawStep> order_splits(const SplitBatch& batch);

// Textures one split samples, resolved by the caller.
struct SplitBindings {
    rhi::TextureH vram;
    rhi::SamplerH vram_sampler;
    rhi::TextureH rgba;  // when SplitDraw::rgba
    rhi::SamplerH rgba_sampler;
    rhi::TextureH shadow;  // when SplitDraw::shadow
    rhi::SamplerH shadow_sampler;
};

// Binds what changed since the last call (tracked in `bound`) and draws the split into the open pass.
struct BoundTextures {
    rhi::TextureH vram, rgba, shadow;
    void reset() { vram = rgba = shadow = {}; }
};
Result<void> issue_split(rhi::RenderPass& pass, rhi::PrimRenderer& prims, const SplitDraw& draw,
                         const SplitBindings& textures, BoundTextures& bound);

class SplitRenderer {
public:
    explicit SplitRenderer(Renderer& owner);
    ~SplitRenderer();

    ShadeInputs& shade() { return shade_; }

    // Sends the batch's vertex stream (draw() does it when it was not done for this batch; the shadow pre-pass
    // needs it first).
    void upload(rhi::CommandList& cmd, const SplitBatch& batch);
    void draw(rhi::CommandList& cmd, SplitBatch& batch);

    // The engine's scene renderer on the device (created on first use; null when it cannot be). ShadowPass draws
    // the scene's casters with it.
    scene::SceneRenderer* scene_renderer();
    // The shadow pre-pass's scene casters (ShadowPass::SceneCasters): uploads of this frame's scene (once per frame
    // serial), outside a pass; the depth of scene item `item` from `light_matrix`, inside the shadow pass.
    bool prepare_scene(rhi::CommandList& cmd);
    // One layer of the shadow map: the depth of the solid texels, or (tint) what the blended ones let through.
    void draw_scene_casters(rhi::RenderPass& pass, std::int32_t item, const std::array<float, 16>& light_matrix,
                            float z_far, bool tint);

    // The light of this frame that takes the flashlight's shadow map while the flashlight is off (lights.json
    // "shadow_map"), where it is shaded without the ray tracer's own shadows.
    std::optional<LightShadow> map_light_shadow();

    struct Stats {
        std::uint64_t splits = 0, draws = 0, scenes = 0, failures = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct Pass;
    void open_pass(rhi::CommandList& cmd, const Split& split);
    void close_pass(rhi::CommandList& cmd);
    void draw_prims(rhi::CommandList& cmd, const Split& split, const SplitBatch& batch, const DrawStep& step);
    void draw_scene(rhi::CommandList& cmd, const Split& split);
    void note_world(const scene::Frame& frame, const scene::View& view);
    SplitContext context(const SplitBatch& batch, bool item_pass) const;
    // Ray tracing of the frame (ray_tracing, native Vulkan only): the acceleration structures, once per frame
    // serial, before the first scene draw into the frame's scene target.
    void trace_frame(rhi::CommandList& cmd, const scene::View& view);

    Renderer& owner_;
    ShadeInputs shade_;
    std::unique_ptr<Pass> pass_;
    std::unique_ptr<scene::SceneRenderer> scene_;
    bool scene_failed_ = false;
    std::uint64_t scene_prepared_ = 0;
    bool scene_prepared_valid_ = false;
    std::unique_ptr<rhi::RayTracer> rt_;
    std::unique_ptr<rhi::RayScene> rt_scene_;
    bool rt_failed_ = false;
    std::uint64_t rt_serial_ = 0;
    bool rt_serial_valid_ = false;
    // The frame's ray-traced results and the view they were traced for: a later scene draw of the frame takes
    // them only with the same target, viewport and projection.
    struct RtFrame {
        rhi::TextureH shadow, reflect, gi;
        bool reflect_mirrors = false;  // the reflection result holds mirror rays, not only the lights' highlights
        rhi::TextureH target;
        std::array<float, 4> viewport{};
        std::array<float, 16> ortho{};
        float draw_ofs_x = 0, draw_ofs_y = 0;
    };
    RtFrame rt_frame_;
    const SplitBatch* uploaded_ = nullptr;
    std::size_t uploaded_count_ = 0;
    Stats stats_;
};

}  // namespace sh::gpu
