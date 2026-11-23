// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Ray tracing on the native Vulkan backend (RENDER.md sections 2.2b and 11): the scene's acceleration
// structures, built from the scene renderer's own vertex and index buffers. Standard KHR extensions only
// (VK_KHR_acceleration_structure, VK_KHR_ray_query); no other backend has it. The interface uses RHI handles, so the
// scene describes its frame without Vulkan types.
#pragma once

#include "formats/common/status.h"
#include "render/rhi/rhi.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace sh::rhi {

// Why ray tracing cannot run (the Options rows show it).
enum class RayTracingSupport : std::uint8_t {
    Available,
    NotNativeVulkan,  // another renderer runs: only the native Vulkan backend has the KHR extensions
    NoHardware,       // the GPU or its driver lacks acceleration structures or ray queries
};
RayTracingSupport rayTracingSupport(Device& device);

// One material range of a mesh: a geometry of its bottom-level structure.
struct RayGeometry {
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    TextureH texture;                          // empty: untextured
    std::uint32_t flags = 0;                   // RayGeometryFlag
    std::array<float, 4> surface{1, 0, 0, 0};  // roughness, metalness, reflective, wet (scene materials.h)
    // RayScene::regions that refine `surface` by texture rectangle (the first that holds the hit's UV wins).
    std::uint32_t regionFirst = 0;
    std::uint32_t regionCount = 0;  // at most 255
};

// A rectangle of a texture with its own material (scene materials.h regions).
struct RayRegion {
    std::array<float, 4> rect{};     // u0, v0, u1, v1 of the texture's normalised UVs
    std::array<float, 4> surface{};  // as RayGeometry::surface
};
enum RayGeometryFlag : std::uint32_t {
    kRayBlendMask = 7,  // scene::Blend of the range (0 opaque with alpha holes)
    kRayTextured = 1u << 3,
    kRayWater = 1u << 4,     // the water surface (scene::water_surface())
    kRayNoShadow = 1u << 5,  // casts no flashlight shadow (PrelitMesh::casts_shadow)
    kRayPrelit = 1u << 6,    // the vertex colour is the lit colour (posed meshes)
};

struct RayMesh {
    // The same key with the same buffers and counts in a later frame is the same mesh: its structure is kept.
    // Null: built for this frame only (the posed meshes, already in view space).
    const void* key = nullptr;
    BufferH vertices;  // float3 position at offset 0 of each vertex
    BufferH indices;   // 32-bit
    std::uint32_t vertexCount = 0;
    std::uint32_t vertexStride = 0;
    std::vector<RayGeometry> geometries;
};

struct RayInstance {
    std::uint32_t mesh = 0;                                               // RayScene::meshes index
    std::array<float, 12> transform{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};  // row-major 3x4: mesh space to view space
};

// What the frame's rays can hit, in the view space of the frame (PS1 axes, 256 units per metre).
inline constexpr std::size_t kMaxRayLights = 32;
inline constexpr std::size_t kMaxRayCeilings = 8;

struct RayScene {
    std::vector<RayMesh> meshes;
    std::vector<RayInstance> instances;
    std::vector<RayRegion> regions;
    void clear() {
        meshes.clear();
        instances.clear();
        regions.clear();
    }
};

class RayTracer {
public:
    // Fails on a device without ray tracing (rayTracingSupport).
    static Result<std::unique_ptr<RayTracer>> create(Device& device);
    ~RayTracer();
    RayTracer(const RayTracer&) = delete;
    RayTracer& operator=(const RayTracer&) = delete;

    // The frame's acceleration structures: the kept bottom-level ones of meshes seen before, new ones (a bounded
    // number per frame, so entering a map spreads its builds), the posed meshes' rebuilt one and the top level.
    // Outside a render pass, after the scene's buffers were uploaded.
    Result<void> build(const RayScene& scene);

    struct Stats {
        std::uint32_t blas = 0;       // kept bottom-level structures
        std::uint32_t built = 0;      // built this frame
        std::uint32_t compacted = 0;  // compacted so far
        std::uint32_t pending = 0;    // instances left out this frame: their structure waits for the build budget
        std::uint32_t instances = 0;
        std::uint64_t triangles = 0;  // in the frame's instances
        std::uint64_t blasBytes = 0;
        std::uint64_t tlasBytes = 0;
        std::uint64_t scratchBytes = 0;
    };
    const Stats& stats() const;

    // The frame's view for the ray-traced effects, in view space.
    struct View {
        TextureH target;                  // the scene target: the results have its size
        std::array<float, 4> viewport{};  // x, y, w, h in target pixels
        std::array<float, 4> pixelRay{};  // view ray of pixel centre (x, y): (x * [0] + [1], y * [2] + [3], 1)
        std::array<double, 12> camera{};  // world to view, row-major 3x4: reprojects the last frame's results
        std::array<float, 4> lens{};      // xyz: the flashlight's lens point, w: lens radius
        std::array<float, 4> beamDir{};   // xyz: beam direction, w: outer cone cosine
        std::array<float, 4> beam{};      // x: inner cone cosine, y: range, z: 1 classic style, w: 1 on
        std::array<float, 4> beamColor{};
        std::array<float, 4> fog{};        // rgb: fog colour, a: strength
        std::array<float, 4> fogParams{};  // FogRampUniforms::params
        std::array<float, 128> fogRamp{};
        std::array<float, 4> ambient{};        // rgb: the map light without the flashlight, w: fog's opaque distance
        std::array<float, 4> up{0, -1, 0, 0};  // xyz: the world's up in view space (wet faces turn to it)
        float nearClip = 24.0f;                // shadow rays stop this short of the lens (the hand that holds it)
        int shadowRays = 2;
        bool shadows = false;
        bool reflections = false;
        bool gi = false;
        float giStrength = 1.0f;
        float maxRoughness = 0.6f;  // rougher surfaces reflect nothing
        // The map's lights and sun (rt_lights, RENDER.md section 10), in view space: their diffuse light goes
        // into Results::gi, their specular and the beam's (GGX) into Results::reflect.
        struct Light {
            std::array<float, 4> position{};   // xyz, w: range
            std::array<float, 4> direction{};  // xyz: a spot's axis (zero for a point light), w: outer cone cosine
            std::array<float, 4> color{};      // rgb, w: inner cone cosine
            float radius = 8.0f;               // the emitter's size
            float nearClip = 0.0f;             // shadow rays stop this short of it
            bool shadows = true;
            bool gi = true;
        };
        bool lights = false;
        std::vector<Light> mapLights;     // at most kMaxRayLights, the nearest first
        std::array<float, 4> sunDir{};    // xyz: the way the light travels, w: 1 on
        std::array<float, 4> sunColor{};  // rgb, w: angular radius
        std::array<float, 4> sky{};       // rgb: the sky's light on surfaces open to it
        // Roofs the cells do not draw (lights.json "ceilings"), view space: a corner and the two edges from it, three
        // per roof, at most kMaxRayCeilings roofs.
        std::vector<std::array<float, 4>> ceilings;
    };
    // rgb: the effect, a: the view z it was traced at (the shading takes it only where its own z matches). Empty
    // handles for effects that are off.
    struct Results {
        TextureH shadow, reflect, gi;
    };
    // Traces and denoises the frame's effects against the structures of the last build(), outside a render pass.
    Result<Results> trace(const View& view);

    struct Impl;

private:
    explicit RayTracer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace sh::rhi
