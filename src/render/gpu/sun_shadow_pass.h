// SPDX-License-Identifier: GPL-3.0-or-later
// The sun's shadow map for the lighting without ray tracing and the god rays (RENDER.md section 7.14): one
// orthographic map along the sun of lights.json, fitted to the part of the view the fog leaves visible (the fog bounds
// everything, so nothing past it needs a texel) and snapped to its texels in world space, so it does not crawl while
// the camera moves. The casters are the flashlight map's (map cells, objects, characters, the opaque PS1 prims) in the
// depth layer only: alpha holes of fences and leaves are not written, so light passes them.
#pragma once

#include "render/gpu/gpu_types.h"
#include "render/rhi/rhi.h"

#include <array>
#include <cstdint>
#include <vector>

namespace sh::scene {
struct Frame;
}

namespace sh::gpu {

class Renderer;

namespace sun_shadow {

// The fitted map of a frame: view space to the casters' clip space (GL convention, column-major), the receivers'
// matrix (rows flipped on a backend with top-left targets), the texel and the depth range in view units.
struct Fit {
    std::array<float, 16> caster{};
    std::array<float, 16> receiver{};
    float texel = 0;
    float depth_range = 0;
};

// The view depth beyond which the fog hides everything (the ramp at full strength or the cull distance), at most
// `cap`. Pure, unit tested.
float fog_reach(const float* params, const float* ramp, float cap);
// The map for the sun travelling along world direction `dir`, the camera, the projection distance `h` and the fog's
// reach. Pure, unit tested.
Fit fit(const std::array<double, 9>& rot, const std::array<double, 3>& trans, const std::array<float, 3>& dir, float h,
        float reach, int side, bool top_left_targets);

}  // namespace sun_shadow

class SunShadowPass {
public:
    explicit SunShadowPass(Renderer& owner);
    ~SunShadowPass();
    SunShadowPass(const SunShadowPass&) = delete;
    SunShadowPass& operator=(const SunShadowPass&) = delete;

    // The settings ask for the map (sun_shadows with map_lights, or god_rays with volumetric_fog) on a backend that
    // runs them.
    bool wanted() const;
    // Builds the map once per frame, from the batch that holds the world, when the frame has a sun. Outside a pass.
    void build(rhi::CommandList& cmd, const SplitBatch& batch);
    // The map of this frame (serial), or none.
    bool ready(std::uint64_t serial) const { return depth_ && built_ && serial_ == serial; }
    rhi::TextureH map() const { return depth_; }
    rhi::SamplerH sampler() const { return sampler_; }
    const sun_shadow::Fit& fit() const { return fit_; }
    int side() const { return size_; }

private:
    bool ensure_target();
    bool ensure_pipeline();

    Renderer& owner_;
    rhi::TextureH depth_, color_;
    int size_ = 0;
    bool target_failed_ = false;
    rhi::SamplerH sampler_;
    rhi::ShaderH vs_, fs_;
    rhi::PipelineH pipeline_;
    bool pipeline_failed_ = false;
    rhi::BufferH buffer_;
    std::uint32_t buffer_bytes_ = 0;
    sun_shadow::Fit fit_;
    bool built_ = false;
    std::uint64_t serial_ = 0;
    std::vector<float> positions_;
    std::vector<std::int32_t> scene_items_;
};

}  // namespace sh::gpu
