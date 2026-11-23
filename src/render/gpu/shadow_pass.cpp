// Flashlight shadow map on the RHI (shadow_pass.h). The PS1 casters are packed on the CPU into a stream of their own
// (only the triangles that may cast), so the pass needs neither the frame's vertex buffer nor a per-run draw; the
// scene renderer's casters are drawn into the same pass through SceneCasters.
#include "render/gpu/shadow_pass.h"

#include "engine/log/sh_log.h"
#include "render/gpu/renderer.h"
#include "render/rhi/shaders/embedded.h"

namespace sh::rhi::embedded {
Shader gpu_shadow_caster_vert();
Shader gpu_shadow_caster_frag();
}  // namespace sh::rhi::embedded

namespace sh::gpu {

namespace {

constexpr std::string_view kVsBlocks[] = {"ShadowFrame"};
constexpr rhi::VertexAttribute kAttributes[] = {{0, rhi::VertexFormat::Float3, 0}};
constexpr std::uint32_t kStride = 3 * sizeof(float);

}  // namespace

ShadowPass::ShadowPass(Renderer& owner) : owner_(owner) {}

ShadowPass::~ShadowPass() {
    rhi::Device& dev = owner_.device();
    release_target();
    if (pipeline_) dev.destroy(pipeline_);
    if (vs_) dev.destroy(vs_);
    if (fs_) dev.destroy(fs_);
    if (sampler_) dev.destroy(sampler_);
    if (buffer_) dev.destroy(buffer_);
}

bool ShadowPass::active() const { return shadow::pass_active(settings_); }

rhi::TextureH ShadowPass::map() const { return depth_; }

void ShadowPass::release_target() {
    rhi::Device& dev = owner_.device();
    if (depth_) dev.destroy(depth_);
    if (color_) dev.destroy(color_);
    depth_ = {};
    color_ = {};
    size_ = 0;
}

bool ShadowPass::ensure_target() {
    // The RHI does not report the driver's texture limit: sizes past 4096 are only clamped to 8192, and a failed
    // allocation falls back to 4096 below.
    const int wanted = post::clampShadowMapSize(settings_.map_size, 0);
    if (requested_ == wanted) return depth_ && sampler_;
    release_target();
    requested_ = wanted;
    rhi::Device& dev = owner_.device();
    for (int size = wanted; size >= 256; size = size > 4096 ? 4096 : 0) {
        const auto side = static_cast<std::uint32_t>(size);
        auto depth = dev.createTexture({rhi::TextureFormat::D24S8, side, side,
                                        rhi::kUsageSampled | rhi::kUsageDepthStencilTarget, "flashlight shadow map"});
        if (!depth) {
            SH_WARN("[SHADOW] %dx%d depth map: %s", size, size, depth.error().c_str());
            continue;
        }
        auto color = dev.createTexture({rhi::TextureFormat::RGBA8, side, side,
                                        rhi::kUsageSampled | rhi::kUsageColorTarget,
                                        "flashlight shadow colour"});
        if (!color) {
            dev.destroy(depth.value());
            SH_WARN("[SHADOW] %dx%d colour target: %s", size, size, color.error().c_str());
            continue;
        }
        depth_ = depth.value();
        color_ = color.value();
        size_ = size;
        SH_LOG("[SHADOW] shadow target ready: %dx%d", size, size);
        break;
    }
    if (!sampler_) {
        auto s = dev.createSampler({});  // nearest, clamp to edge: receivers outside the frustum never sample
        if (s) sampler_ = s.value();
    }
    return depth_ && sampler_;
}

bool ShadowPass::ensure_pipeline() {
    if (pipeline_) return true;
    if (pipeline_failed_) return false;
    pipeline_failed_ = true;  // logged once; cleared below on success
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
    if (!vsh) {
        SH_WARN("[SHADOW] caster shader: %s", vsh.error().c_str());
        return false;
    }
    vs_ = vsh.value();
    rhi::ShaderDesc fs;
    fs.stage = rhi::ShaderStage::Fragment;
    fs.glsl = fsrc.glsl;
    fs.spirv = fsrc.spirv;
    fs.name = "shadow_caster.frag";
    auto fsh = dev.createShader(fs);
    if (!fsh) {
        SH_WARN("[SHADOW] caster shader: %s", fsh.error().c_str());
        return false;
    }
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
    p.name = "flashlight shadow casters";
    auto pipe = dev.createPipeline(p);
    if (!pipe) {
        SH_WARN("[SHADOW] caster pipeline: %s", pipe.error().c_str());
        return false;
    }
    pipeline_ = pipe.value();
    pipeline_failed_ = false;
    return true;
}

bool ShadowPass::ensure_buffer(std::uint32_t bytes) {
    if (buffer_ && buffer_bytes_ >= bytes) return true;
    rhi::Device& dev = owner_.device();
    if (buffer_) dev.destroy(buffer_);
    buffer_ = {};
    buffer_bytes_ = 0;
    std::uint32_t cap = 64u * 1024u;
    while (cap < bytes) cap *= 2;
    auto b = dev.createBuffer({rhi::BufferUsage::Vertex, cap, "flashlight shadow casters"});
    if (!b) {
        SH_WARN("[SHADOW] caster buffer (%u bytes): %s", cap, b.error().c_str());
        return false;
    }
    buffer_ = b.value();
    buffer_bytes_ = cap;
    return true;
}

void ShadowPass::build(rhi::CommandList& cmd, const SplitBatch& batch) {
    if (!active()) return;
    // A batch without casters (the UI, the water, 2D-only frames) keeps the map the world batch built.
    if (!shadow::collect_casters(batch, settings_.near_z, &positions_, &scene_items_)) return;
    if (!ensure_target() || !ensure_pipeline()) return;

    const post::ShadowLight light = post::buildShadowLight(settings_.light);
    light_ = light.matrix;
    z_near_ = light.zNear;
    z_far_ = light.zFar;

    const auto bytes = static_cast<std::uint32_t>(positions_.size() * sizeof(float));
    const bool draw_prims = bytes > 0 && ensure_buffer(bytes);
    if (draw_prims) cmd.uploadBuffer(buffer_, 0, std::as_bytes(std::span(positions_)));
    const bool draw_scene = !scene_items_.empty() && scene_.draw;
    if (draw_scene && scene_.prepare) scene_.prepare(cmd);

    rhi::RenderPassDesc desc;
    desc.color = {color_, rhi::LoadOp::Clear, rhi::StoreOp::Store, {1.f, 1.f, 1.f, 1.f}};
    desc.depthStencil = {depth_, rhi::LoadOp::Clear, rhi::StoreOp::Store, 1.f, 0};
    desc.name = "shadow";
    rhi::RenderPass* pass = cmd.beginRenderPass(desc);
    if (!pass) {
        SH_WARN("[SHADOW] the shadow pass could not begin");
        return;
    }
    const float side = static_cast<float>(size_);
    pass->setViewport({0.f, 0.f, side, side, 0.f, 1.f});
    if (draw_prims) {
        pass->bindPipeline(pipeline_);
        pass->bindVertexBuffer(buffer_, 0);
        pass->pushUniforms(rhi::ShaderStage::Vertex, 0, std::as_bytes(std::span(light_)));
        pass->draw(static_cast<std::uint32_t>(positions_.size() / 3), 0);
    }
    if (draw_scene)
    {
        // Every solid caster first: the transmittance layer is depth-tested against them.
        for (std::int32_t item : scene_items_) scene_.draw(*pass, item, light_, z_far_, false);
        for (std::int32_t item : scene_items_) scene_.draw(*pass, item, light_, z_far_, true);
    }
    cmd.endRenderPass();
}

void ShadowPass::shader_inputs(rhi::PrimFsUniforms* fs) const {
    const bool on = shadow::sampling_on(settings_, static_cast<bool>(depth_));
    const bool top_left = owner_.device().caps().fragCoordTopLeft;
    shadow::fill_inputs(settings_, on, shadow::receiver_matrix(light_, top_left), size_, z_near_, z_far_, fs);
}

}  // namespace sh::gpu
