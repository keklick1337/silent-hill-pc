// Pure parts of the flashlight shadow pass (shadow_pass.h): which triangles cast, the caster stream, the gates and
// the fragment-block inputs. No device: unit tested in tests/render/gpu_shadow_test.cpp.
#include "render/gpu/shadow_pass.h"

#include <algorithm>
#include <cmath>

namespace sh::gpu::shadow {

namespace {

// A coordinate past this is a broken projection: it would splat across the whole map as a false shadow wedge.
constexpr float kMaxCoord = 1.0e6f;

bool vertex_can_cast(const Vertex& v, float near_z) {
    // Written so that a NaN marker passes, while a NaN position never does.
    const bool provenance = !(v.ny < 0.5f) && !(v.nx > 0.5f);
    if (!provenance) return false;
    if (!std::isfinite(v.vsx) || !std::isfinite(v.vsy) || !std::isfinite(v.vsz)) return false;
    if (!(v.vsz > 0.f) || v.vsz > kMaxCoord) return false;
    if (std::fabs(v.vsx) > kMaxCoord || std::fabs(v.vsy) > kMaxCoord) return false;
    // Geometry clipping the lens projects to an extreme map coordinate.
    return !(v.vsz < near_z);
}

}  // namespace

bool triangle_can_cast(const Vertex* tri, float near_z) {
    return vertex_can_cast(tri[0], near_z) && vertex_can_cast(tri[1], near_z) && vertex_can_cast(tri[2], near_z);
}

void caster_runs(std::span<const Vertex> stream, std::uint32_t first, std::uint32_t count, float near_z,
                 std::vector<Run>* out) {
    if (first >= stream.size()) return;
    if (count > stream.size() - first) count = static_cast<std::uint32_t>(stream.size() - first);
    const std::uint32_t whole = count - count % 3;
    Run run;
    bool open = false;
    for (std::uint32_t at = 0; at < whole; at += 3) {
        if (triangle_can_cast(&stream[first + at], near_z)) {
            if (!open) run = {first + at, 0};
            open = true;
            run.count += 3;
        } else if (open) {
            out->push_back(run);
            open = false;
        }
    }
    if (open) out->push_back(run);
}

bool split_casts(const Split& split) {
    return split.kind == Split::Kind::Prims && split.blend == Blend::None && split.count >= 3;
}

bool collect_casters(const SplitBatch& batch, float near_z, std::vector<float>* positions,
                     std::vector<std::int32_t>* scene_items) {
    positions->clear();
    scene_items->clear();
    bool any_scene = false;
    std::vector<Run> runs;
    for (const Split& split : batch.splits) {
        if (split.kind == Split::Kind::Scene) {
            any_scene = true;
            scene_items->push_back(split.scene_item);
            continue;
        }
        if (!split_casts(split)) continue;
        runs.clear();
        caster_runs(batch.vertices, split.first, split.count, near_z, &runs);
        for (const Run& r : runs)
            for (std::uint32_t i = 0; i < r.count; ++i) {
                const Vertex& v = batch.vertices[r.first + i];
                positions->insert(positions->end(), {v.vsx, v.vsy, v.vsz});
            }
    }
    return any_scene || !positions->empty();
}

bool pass_active(const ShadowSettings& s) {
    return s.use_shadows && s.config_shadows && s.per_pixel && s.flashlight_active && s.allowed && !s.frozen;
}

bool sampling_on(const ShadowSettings& s, bool have_map) {
    return s.use_shadows && s.config_shadows && s.per_pixel && s.flashlight_active && have_map && !s.frozen;
}

std::array<float, 16> receiver_matrix(const std::array<float, 16>& light, bool top_left_targets) {
    std::array<float, 16> m = light;
    if (top_left_targets)
        for (int col = 0; col < 4; ++col) m[static_cast<std::size_t>(col * 4 + 1)] = -m[static_cast<std::size_t>(col * 4 + 1)];
    return m;
}

void fill_inputs(const ShadowSettings& s, bool on, const std::array<float, 16>& receiver, int map_size, float z_near,
                 float z_far, rhi::PrimFsUniforms* fs) {
    // prog.w doubles as the PCF kernel: the shaders sample (2 * (w - 1) + 1)^2 texels, 0 = no shadows.
    fs->prog[3] = on ? 1.f + static_cast<float>(std::clamp(s.filter, 0, 2)) : 0.f;
    if (!on) return;
    fs->shadowMatrix = receiver;
    fs->shadowP = {s.bias, s.normal_offset, s.strength, s.fade_distance};
    const float texel = map_size > 0 ? 1.f / static_cast<float>(map_size) : 0.f;
    fs->shadowQ = {texel, texel, z_near, z_far};
}

}  // namespace sh::gpu::shadow
