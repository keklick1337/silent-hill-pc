// Flashlight shadow map (RENDER.md section 3): a depth pre-pass from the light over the opaque triangles that
// can cast (shadow::triangle_can_cast) and the scene's casters, sampled by prim_batch.frag and the scene shader.
//
// The map is a D24S8 depth texture (prim_batch.frag reads window depth from s_shadow.r) with an RGBA8 colour layer of
// the same size: what the translucent surfaces of the scene let through (rgb, cleared to white) and the distance of
// the nearest one along the light over the far plane (a, cleared to 1). The solid casters do not write it. The scene
// renderer's caster pipelines are built for RGBA8 + D24S8.
#pragma once

#include "render/gpu/gpu_types.h"
#include "render/post/post.h"
#include "render/rhi/prim_batch.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace sh::gpu {

class Renderer;

// What the shadow pass reads from the configuration and the game each frame.
struct ShadowSettings {
    bool use_shadows = false;        // flashlight_mode asks for shadows (render::flashlight().shadows)
    bool config_shadows = true;      // the preset allows the pass (render::graphics().flashlight_shadows)
    bool per_pixel = false;          // per-pixel flashlight (render::flashlight().per_pixel)
    bool flashlight_active = false;  // the game pushed a light this frame
    bool allowed = false;            // settled gameplay: the pre-pass may run (render::flashlight().shadows_allowed)
    bool frozen = false;             // the frozen last frame is shown (render::feedback_control().present_last_frame)
    int map_size = 1024;             // requested; clamped by post::clampShadowMapSize
    int filter = 1;                  // PCF kernel 0 / 1 / 2 = 2x2 / 3x3 / 5x5 texels (render::flashlight().shadow_filter)
    float bias = 0.0018f;            // depth compare bias in light clip [0, 1]
    float normal_offset = 0.f;
    float strength = 1.f;
    float fade_distance = 0.f;
    float near_z = 16.f;             // precise-vertex near plane: casters closer than this are dropped
    post::ShadowLightInput light;    // inputs of the light matrix
};

// The settings as the render state holds them (render_state.h; shadow_state.cpp).
ShadowSettings shadow_settings_from_state();

// Pure parts, unit tested (shadow_casters.cpp).
namespace shadow {

// One triangle (three consecutive vertices) may cast: every vertex carries a validated view position (ny), is not
// suppressed (nx), and is finite, sane in size and beyond the near plane.
bool triangle_can_cast(const Vertex* tri, float near_z);

struct Run {
    std::uint32_t first = 0, count = 0;  // stream vertices, whole triangles
    friend bool operator==(const Run&, const Run&) = default;
};
// The contiguous runs of casting triangles in vertices [first, first + count) of `stream` (DrawShadowCasters).
void caster_runs(std::span<const Vertex> stream, std::uint32_t first, std::uint32_t count, float near_z,
                 std::vector<Run>* out);
// The split casts at all: opaque PS1 prims (every blend mode is skipped, the muzzle flash's Average included).
bool split_casts(const Split& split);
// The view positions of every casting triangle of `batch`, packed for the caster shader; `scene_items` gets the
// scene item of every Scene split (they cast through the scene renderer). Returns whether the pass has anything.
bool collect_casters(const SplitBatch& batch, float near_z, std::vector<float>* positions,
                     std::vector<std::int32_t>* scene_items);

// The pre-pass runs this frame.
bool pass_active(const ShadowSettings& s);
// The receivers sample a map (no `allowed`: a map built on the last gameplay frame stays right while paused).
bool sampling_on(const ShadowSettings& s, bool have_map);
// The light matrix the receivers use. On a backend whose render targets have a top-left origin the map rows are
// stored flipped against GL, so the receiver's light-clip Y is negated to sample the row the caster wrote.
std::array<float, 16> receiver_matrix(const std::array<float, 16>& light, bool top_left_targets);
// The fragment-block shadow inputs (prog.w, shadowMatrix, shadowP, shadowQ).
void fill_inputs(const ShadowSettings& s, bool on, const std::array<float, 16>& receiver, int map_size, float z_near,
                 float z_far, rhi::PrimFsUniforms* fs);

}  // namespace shadow

// The scene renderer's casters: the owner of the scene renderer registers these.
// `prepare` runs outside the pass (uploads), `draw` inside the shadow pass with the casters' light matrix.
struct SceneCasters {
    std::function<void(rhi::CommandList& cmd)> prepare;
    std::function<void(rhi::RenderPass& pass, std::int32_t scene_item, const std::array<float, 16>& light_matrix,
                       float z_far, bool tint)>
        draw;
};

class ShadowPass {
public:
    explicit ShadowPass(Renderer& owner);
    ~ShadowPass();
    ShadowPass(const ShadowPass&) = delete;
    ShadowPass& operator=(const ShadowPass&) = delete;

    void set_settings(const ShadowSettings& settings) { settings_ = settings; }
    const ShadowSettings& settings() const { return settings_; }
    void set_scene_casters(SceneCasters casters) { scene_ = std::move(casters); }

    bool active() const;
    // Builds the map from `batch` when active (opened lazily on the first caster; the last map stays otherwise).
    // Outside a render pass.
    void build(rhi::CommandList& cmd, const SplitBatch& batch);
    rhi::TextureH map() const;
    // The transmittance layer of the same map (linear filtering is fine: rgb and a distance).
    rhi::TextureH tint() const { return color_; }
    rhi::SamplerH sampler() const { return sampler_; }  // nearest, clamped
    // The shadow inputs of the fragment block (matrix, bias, strength, map size).
    void shader_inputs(rhi::PrimFsUniforms* fs) const;

private:
    bool ensure_target();
    bool ensure_pipeline();
    bool ensure_buffer(std::uint32_t bytes);
    void release_target();

    Renderer& owner_;
    ShadowSettings settings_;
    SceneCasters scene_;
    rhi::TextureH depth_, color_;
    int size_ = 0;       // allocated side
    int requested_ = 0;  // clamped side the allocation was made (or failed) for
    rhi::SamplerH sampler_;
    rhi::ShaderH vs_, fs_;
    rhi::PipelineH pipeline_;
    bool pipeline_failed_ = false;
    rhi::BufferH buffer_;
    std::uint32_t buffer_bytes_ = 0;
    std::array<float, 16> light_{1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
    float z_near_ = 20.f, z_far_ = 5200.f;
    std::vector<float> positions_;
    std::vector<std::int32_t> scene_items_;
};

}  // namespace sh::gpu
