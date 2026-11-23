// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Ambient occlusion and froxel volumetric fog of the native Vulkan backend (RENDER.md sections 7.10 and
// 7.12). Compute passes over the world's depth, then one composite drawn over the scene colour:
//   colour' = colour * k + fog * (1 - T) * (1 - k) + S
// k is the occlusion (1 = none) faded out where the flashlight lights the surface, T the soft_fog transmittance of
// the pixel's depth, S the flashlight light the fog scatters towards the eye between the eye and the surface. The
// fog's density along each ray is the original ramp: a slice of the volume scatters exactly the share of light the
// ramp takes away over it, so the fog amount at every depth stays the original's and S is zero at scattering 0.
#pragma once

#include "render/rhi/prim_batch.h"
#include "render/rhi/rhi.h"

#include <array>
#include <memory>
#include <string>

namespace sh::rhi {

struct AtmosphereInputs {
    TextureH color;  // the scene target the world was drawn into
    TextureH depth;  // its depth, created sampled
    // The world's viewport in the target (pixels, top-left origin).
    float viewport_x = 0, viewport_y = 0, viewport_w = 0, viewport_h = 0;
    std::array<float, 4> proj{};  // view to NDC: ndc = xy * (v.xy / v.z) + zw (NDC +Y up)
    float sz_max = 65535;         // window depth 1 is this view depth
    PrimFsUniforms fs;            // fog colour and strength, flashlight, shadow matrix
    FogRampUniforms fog;          // the fog ramp
    TextureH shadow_map;          // flashlight shadow depth; empty: the beam is not shadowed
    bool ao = false;
    float ao_strength = 0.35f;  // 0..1
    float ao_radius = 96.0f;    // view units (256 per metre)
    bool volumetric = false;
    float scatter = 0.25f;    // share of the beam the fog scatters, 0..1
    float anisotropy = 0.2f;  // Henyey-Greenstein g
    // The map's lights the fog scatters too (rt_lights, lights.json), in view space, unshadowed: xyz and range,
    // rgb colour. The nearest kAtmosphereLights.
    int lights = 0;
    std::array<std::array<float, 4>, 8> lightPos{};
    std::array<std::array<float, 4>, 8> lightColor{};
    // God rays (god_rays, RENDER.md 7.14): the sun scatters where its shadow map sees it. sunDir xyz the way the
    // light travels in view space, w the map's depth range; the matrix takes view space to the map's clip space.
    bool sun = false;
    TextureH sun_map;
    std::array<float, 4> sunDir{};
    std::array<float, 4> sunColor{};
    std::array<float, 16> sunMatrix{};
};
inline constexpr int kAtmosphereLights = 8;

class Atmosphere {
public:
    virtual ~Atmosphere() = default;
    // Records the passes into the frame being recorded, outside any render pass, at most once per frame (a second
    // call in the same frame does nothing); the profiler lists their GPU time as "atmosphere". An error leaves the
    // image as it was.
    virtual Result<void> apply(const AtmosphereInputs& in) = 0;
};

// Empty `reason` and a working object, or null and why not (another backend, no compute on the queue, a format
// without the storage use).
std::unique_ptr<Atmosphere> createAtmosphere(Device& device, std::string* reason);

}  // namespace sh::rhi
