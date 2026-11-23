// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/gpu/sun_shadow_pass.h"

#include "engine/log/sh_log.h"
#include "render/gpu/atmosphere_pass.h"
#include "render/gpu/renderer.h"
#include "render/gpu/shadow_pass.h"
#include "render/gpu/split_renderer.h"
#include "render/gpu/targets.h"
#include "render/rhi/shaders/embedded.h"
#include "render/scene/scene.h"
#include "render/shaderstate/shaderstate.h"

#include <algorithm>
#include <cmath>

namespace sh::rhi::embedded {
Shader gpu_shadow_caster_vert();
Shader gpu_shadow_caster_frag();
}  // namespace sh::rhi::embedded

namespace sh::gpu {

namespace sun_shadow {

namespace {
// Casters up to this far towards the sun from the fitted box (32 m): trees and houses beside the street.
constexpr double kReachTowardsSun = 8192.0;
// The fog's reach is never taken below this (8 m), so a dense fog still shades Harry's surroundings.
constexpr float kMinReach = 2048.0f;
// The half-width of the picture over the projection distance: a 16:9 view of the 320-unit wide screen.
constexpr double kHalfWidth = 160.0 * (16.0 / 9.0) / (4.0 / 3.0);
constexpr double kHalfHeight = 120.0;
}  // namespace

float fog_reach(const float* params, const float* ramp, float cap) {
    if (params[0] < 0.5f || params[1] <= 0.0f) return cap;
    float far = 128.0f / params[1];
    for (int i = 0; i < 128; ++i) {
        if (std::clamp(ramp[i] * 16.0f + params[2], 0.0f, 4096.0f) >= 4092.0f) {
            far = static_cast<float>(i) / params[1];
            break;
        }
    }
    if (params[3] > 0.0f) far = std::min(far, params[3]);
    return std::clamp(far, kMinReach, cap);
}

Fit fit(const std::array<double, 9>& rot, const std::array<double, 3>& trans, const std::array<float, 3>& dir, float h,
        float reach, int side, bool top_left_targets) {
    Fit out;
    // The light's axes in world space (fixed for the location), then turned into view space: f along the light, r
    // and u across it.
    double f[3] = {dir[0], dir[1], dir[2]};
    const double fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fl < 1e-9 || side <= 0) return out;
    for (double& v : f) v /= fl;
    const double up[3] = {std::abs(f[2]) < 0.9 ? 0.0 : 1.0, 0.0, std::abs(f[2]) < 0.9 ? 1.0 : 0.0};
    double r[3] = {up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0]};
    const double rl = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    for (double& v : r) v /= rl;
    const double u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    auto to_view = [&](const double* w, double* v) {
        for (int i = 0; i < 3; ++i)
            v[i] = rot[static_cast<std::size_t>(i * 3)] * w[0] + rot[static_cast<std::size_t>(i * 3 + 1)] * w[1] +
                   rot[static_cast<std::size_t>(i * 3 + 2)] * w[2];
    };
    double rv[3], uv[3], fv[3];
    to_view(r, rv);
    to_view(u, uv);
    to_view(f, fv);
    // The smallest sphere on the view axis around the eye and the four corners at the fog's reach.
    const double hp = std::max(static_cast<double>(h), 1.0);
    const double tx = kHalfWidth / hp, ty = kHalfHeight / hp, d = reach;
    const double z0 = std::min(d, d * (1.0 + tx * tx + ty * ty) * 0.5);
    const double radius = std::max(z0, std::sqrt((d - z0) * (d - z0) + d * d * (tx * tx + ty * ty)));
    const double texel = 2.0 * radius / side;
    // Light coordinates of a view point v: dot(axis, v - trans), the world position along the world axis.
    auto light = [&](const double* a, double vz) {
        return a[2] * vz - (a[0] * trans[0] + a[1] * trans[1] + a[2] * trans[2]);
    };
    const double cx = std::round(light(rv, z0) / texel) * texel;
    const double cy = std::round(light(uv, z0) / texel) * texel;
    const double cz = light(fv, z0);
    const double z_near = cz - radius - kReachTowardsSun, z_far = cz + radius;
    const double range = z_far - z_near;
    std::array<double, 16> m{};
    const double ofs_t[3] = {-(rv[0] * trans[0] + rv[1] * trans[1] + rv[2] * trans[2]),
                             -(uv[0] * trans[0] + uv[1] * trans[1] + uv[2] * trans[2]),
                             -(fv[0] * trans[0] + fv[1] * trans[1] + fv[2] * trans[2])};
    for (int c = 0; c < 3; ++c) {
        m[static_cast<std::size_t>(c * 4 + 0)] = rv[c] / radius;
        m[static_cast<std::size_t>(c * 4 + 1)] = uv[c] / radius;
        m[static_cast<std::size_t>(c * 4 + 2)] = 2.0 * fv[c] / range;
    }
    m[12] = (ofs_t[0] - cx) / radius;
    m[13] = (ofs_t[1] - cy) / radius;
    m[14] = 2.0 * (ofs_t[2] - z_near) / range - 1.0;
    m[15] = 1.0;
    for (std::size_t i = 0; i < 16; ++i) out.caster[i] = static_cast<float>(m[i]);
    out.receiver = shadow::receiver_matrix(out.caster, top_left_targets);
    out.texel = static_cast<float>(texel);
    out.depth_range = static_cast<float>(range);
    return out;
}

}  // namespace sun_shadow

namespace {
constexpr std::string_view kVsBlocks[] = {"ShadowFrame"};
constexpr rhi::VertexAttribute kAttributes[] = {{0, rhi::VertexFormat::Float3, 0}};
constexpr std::uint32_t kStride = 3 * sizeof(float);
constexpr int kSide = 2048;
constexpr float kReachCap = 16384.0f;
}  // namespace

SunShadowPass::SunShadowPass(Renderer& owner) : owner_(owner) {}

SunShadowPass::~SunShadowPass() {
    rhi::Device& dev = owner_.device();
    if (depth_) dev.destroy(depth_);
    if (color_) dev.destroy(color_);
    if (pipeline_) dev.destroy(pipeline_);
    if (vs_) dev.destroy(vs_);
    if (fs_) dev.destroy(fs_);
    if (sampler_) dev.destroy(sampler_);
    if (buffer_) dev.destroy(buffer_);
}

bool SunShadowPass::wanted() const {
    const render::AtmosphereSettings& s = render::atmosphere();
    const bool lit = s.map_lights != 0 && s.sun_shadows != 0;
    const bool rays = s.god_rays != 0 && s.volumetric_fog != 0;
    return (lit || rays) && owner_.atmosphere().available();
}

bool SunShadowPass::ensure_target() {
    if (depth_) return true;
    if (target_failed_) return false;
    target_failed_ = true;
    rhi::Device& dev = owner_.device();
    const auto side = static_cast<std::uint32_t>(kSide);
    auto depth = dev.createTexture(
        {rhi::TextureFormat::D24S8, side, side, rhi::kUsageSampled | rhi::kUsageDepthStencilTarget, "sun shadow map"});
    if (!depth) {
        SH_WARN("[SHADOW] sun map %dx%d: %s", kSide, kSide, depth.error().c_str());
        return false;
    }
    // The casters' pipelines draw into an RGBA8 target with the depth (write-masked off).
    auto color =
        dev.createTexture({rhi::TextureFormat::RGBA8, side, side, rhi::kUsageColorTarget, "sun shadow colour"});
    if (!color) {
        dev.destroy(depth.value());
        SH_WARN("[SHADOW] sun map colour %dx%d: %s", kSide, kSide, color.error().c_str());
        return false;
    }
    depth_ = depth.value();
    color_ = color.value();
    size_ = kSide;
    if (!sampler_) {
        auto s = dev.createSampler({});
        if (s) sampler_ = s.value();
    }
    target_failed_ = !sampler_;
    SH_LOG("[SHADOW] sun shadow map ready: %dx%d", kSide, kSide);
    return !target_failed_;
}

bool SunShadowPass::ensure_pipeline() {
    if (pipeline_) return true;
    if (pipeline_failed_) return false;
    pipeline_failed_ = true;
    rhi::Device& dev = owner_.device();
    const rhi::embedded::Shader vsrc = rhi::embedded::gpu_shadow_caster_vert();
    const rhi::embedded::Shader fsrc = rhi::embedded::gpu_shadow_caster_frag();
    rhi::ShaderDesc vs;
    vs.stage = rhi::ShaderStage::Vertex;
    vs.glsl = vsrc.glsl;
    vs.spirv = vsrc.spirv;
    vs.uniformBlockNames = kVsBlocks;
    vs.name = "shadow_caster.vert";
    auto vsh = dev.createShader(vs);
    if (!vsh) return false;
    vs_ = vsh.value();
    rhi::ShaderDesc fs;
    fs.stage = rhi::ShaderStage::Fragment;
    fs.glsl = fsrc.glsl;
    fs.spirv = fsrc.spirv;
    fs.name = "shadow_caster.frag";
    auto fsh = dev.createShader(fs);
    if (!fsh) return false;
    fs_ = fsh.value();
    rhi::PipelineDesc p;
    p.vertexShader = vs_;
    p.fragmentShader = fs_;
    p.vertexLayout = {kStride, kAttributes};
    p.blend.writeMask = 0;
    p.depthStencil.depthTest = true;
    p.depthStencil.depthWrite = true;
    p.depthStencil.depthCompare = rhi::CompareOp::Less;
    p.cull = rhi::CullMode::None;
    p.colorFormat = rhi::TextureFormat::RGBA8;
    p.hasDepthStencil = true;
    p.depthStencilFormat = rhi::TextureFormat::D24S8;
    p.name = "sun shadow casters";
    auto pipe = dev.createPipeline(p);
    if (!pipe) {
        SH_WARN("[SHADOW] sun caster pipeline: %s", pipe.error().c_str());
        return false;
    }
    pipeline_ = pipe.value();
    pipeline_failed_ = false;
    return true;
}

namespace {

// The roofs the cells do not draw (lights.json "ceilings") as casters in view space, as the batch's triangles are:
// two triangles of each winding, whatever the pipeline culls.
void add_ceilings(const scene::Frame& frame, std::vector<float>* positions) {
    auto put = [&](float x, float y, float z) {
        const double w[3] = {x, y, z};
        for (std::size_t r = 0; r < 3; ++r) {
            double v = frame.camera.trans[r];
            for (std::size_t k = 0; k < 3; ++k) v += frame.camera.rot[r * 3 + k] * w[k];
            positions->push_back(static_cast<float>(v));
        }
    };
    // Only the part around the eye: a roof as wide as the map would leave the map's depth range along the sun.
    float eye[3];
    for (std::size_t k = 0; k < 3; ++k) {
        double e = 0.0;
        for (std::size_t r = 0; r < 3; ++r) e -= frame.camera.rot[r * 3 + k] * frame.camera.trans[r];
        eye[k] = static_cast<float>(e);
    }
    const float span = 1.5f * kReachCap;
    for (const std::array<float, 5>& c : frame.sun.ceilings) {
        const float x0 = std::max(c[0], eye[0] - span), z0 = std::max(c[1], eye[2] - span);
        const float x1 = std::min(c[2], eye[0] + span), z1 = std::min(c[3], eye[2] + span), y = c[4];
        if (x1 <= x0 || z1 <= z0) continue;
        const float quad[6][2] = {{x0, z0}, {x1, z0}, {x1, z1}, {x0, z0}, {x1, z1}, {x0, z1}};
        for (const auto& p : quad) put(p[0], y, p[1]);
        for (int i = 5; i >= 0; --i) put(quad[i][0], y, quad[i][1]);
    }
}

}  // namespace

void SunShadowPass::build(rhi::CommandList& cmd, const SplitBatch& batch) {
    const scene::Frame& frame = scene::current_frame();
    if (!frame.world || !frame.sun.on || (built_ && serial_ == frame.serial) || !wanted()) return;
    if (!shadow::collect_casters(batch, 0.0f, &positions_, &scene_items_)) return;
    // Only the batch that draws the world builds it: the map cells are its casters.
    if (std::find(scene_items_.begin(), scene_items_.end(), -1) == scene_items_.end()) return;
    if (!ensure_target() || !ensure_pipeline()) return;
    add_ceilings(frame, &positions_);

    const shaderstate::FogRamp& ramp = shaderstate::fogRamp();
    const float reach = sun_shadow::fog_reach(ramp.params(), ramp.ramp(), kReachCap);
    fit_ = sun_shadow::fit(frame.camera.rot, frame.camera.trans, frame.sun.direction, frame.h, reach, size_,
                           owner_.device().caps().fragCoordTopLeft);
    if (fit_.texel <= 0.0f) return;

    const auto bytes = static_cast<std::uint32_t>(positions_.size() * sizeof(float));
    bool draw_prims = bytes > 0;
    if (draw_prims && (!buffer_ || buffer_bytes_ < bytes)) {
        rhi::Device& dev = owner_.device();
        if (buffer_) dev.destroy(buffer_);
        buffer_ = {};
        std::uint32_t cap = 64u * 1024u;
        while (cap < bytes) cap *= 2;
        auto b = dev.createBuffer({rhi::BufferUsage::Vertex, cap, "sun shadow casters"});
        buffer_bytes_ = b ? cap : 0;
        if (b) buffer_ = b.value();
        draw_prims = static_cast<bool>(buffer_);
    }
    if (draw_prims) cmd.uploadBuffer(buffer_, 0, std::as_bytes(std::span(positions_)));
    SplitRenderer& splits = owner_.splits();
    splits.prepare_scene(cmd);

    rhi::RenderPassDesc desc;
    desc.color = {color_, rhi::LoadOp::DontCare, rhi::StoreOp::DontCare, {1.f, 1.f, 1.f, 1.f}};
    desc.depthStencil = {depth_, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.f, 0};
    desc.name = "sun shadow";
    rhi::RenderPass* pass = cmd.beginRenderPass(desc);
    if (!pass) return;
    const float side = static_cast<float>(size_);
    pass->setViewport({0.f, 0.f, side, side, 0.f, 1.f});
    if (draw_prims) {
        pass->bindPipeline(pipeline_);
        pass->bindVertexBuffer(buffer_, 0);
        pass->pushUniforms(rhi::ShaderStage::Vertex, 0, std::as_bytes(std::span(fit_.caster)));
        pass->draw(static_cast<std::uint32_t>(positions_.size() / 3), 0);
    }
    for (std::int32_t item : scene_items_) splits.draw_scene_casters(*pass, item, fit_.caster, 1.0f, false);
    cmd.endRenderPass();
    built_ = true;
    serial_ = frame.serial;
}

}  // namespace sh::gpu
