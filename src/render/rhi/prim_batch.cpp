#include "render/rhi/prim_batch.h"

#include "render/rhi/shaders/embedded.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>

namespace sh::rhi {

namespace {

constexpr VertexAttribute kAttributes[] = {
    {0, VertexFormat::Short4, offsetof(PrimVertex, x)},
    {1, VertexFormat::Float, offsetof(PrimVertex, z)},
    {2, VertexFormat::UByte4, offsetof(PrimVertex, u)},
    {3, VertexFormat::UByte4Norm, offsetof(PrimVertex, r)},
    {4, VertexFormat::Byte4, offsetof(PrimVertex, tcx)},
    {5, VertexFormat::Float3, offsetof(PrimVertex, ppx)},
    {6, VertexFormat::Float3, offsetof(PrimVertex, nx)},
    {7, VertexFormat::Float3, offsetof(PrimVertex, vsx)},
    {8, VertexFormat::Float, offsetof(PrimVertex, geom3d)},
    {9, VertexFormat::UByte4, offsetof(PrimVertex, ulo)},
};

// WorldFog is one block in both stages: GL links same-named blocks into one, and its members may not repeat in
// two blocks of a program. The GL backend binds it at the fragment slot; SPIR-V keeps both slots.
constexpr std::string_view kVsBlocks[] = {"VsFrame", "WorldFog"};
constexpr std::string_view kFsBlocks[] = {"FsFrame", "WorldFog"};
constexpr std::string_view kFsSamplers[] = {"s_vram", "s_rgba", "s_shadow", "s_fogRamp"};

// AddQuarter reads the hardware's B + F/4 through a constant of 0.25. The other blended modes report the same
// constant so that switching between blended modes never changes the blend constants (the frame's other GL
// users expect them there).
constexpr float kQuarterBlendAlpha = 0.25f;

bool sameBytes(const void* a, const void* b, std::size_t n) { return std::memcmp(a, b, n) == 0; }

}  // namespace

std::array<float, 16> orthoProjection(float left, float right, float bottom, float top, float znear, float zfar) {
    const float a = 2.0f / (right - left);
    const float b = 2.0f / (top - bottom);
    const float c = 2.0f / (znear - zfar);
    const float x = (left + right) / (left - right);
    const float y = (bottom + top) / (bottom - top);
    const float z = (znear + zfar) / (znear - zfar);
    return {a, 0, 0, 0, 0, b, 0, 0, 0, 0, c, 0, x, y, z, 1};
}

BlendState primBlendState(BlendMode blend) {
    BlendState s;
    if (blend == BlendMode::None) return s;
    s.enable = true;
    s.colorOp = blend == BlendMode::Subtract ? BlendOp::ReverseSubtract : BlendOp::Add;
    s.alphaOp = BlendOp::Add;
    BlendFactor src = BlendFactor::One, dst = BlendFactor::Zero;
    switch (blend) {
        case BlendMode::Average:
            src = BlendFactor::SrcAlpha;
            dst = BlendFactor::OneMinusSrcAlpha;
            break;
        case BlendMode::Add:
            src = BlendFactor::One;
            dst = BlendFactor::SrcAlpha;
            break;
        case BlendMode::Subtract:
            src = BlendFactor::One;
            dst = BlendFactor::One;
            break;
        case BlendMode::AddQuarter:
            src = BlendFactor::ConstantColor;
            dst = BlendFactor::One;
            break;
        case BlendMode::ConstantAlpha:
            src = BlendFactor::ConstantColor;
            dst = BlendFactor::OneMinusConstantColor;
            break;
        case BlendMode::None:
            break;
    }
    s.srcColor = s.srcAlpha = src;
    s.dstColor = s.dstAlpha = dst;
    return s;
}

std::array<float, 4> primBlendConstants(BlendMode blend, float feedbackDamp) {
    switch (blend) {
        case BlendMode::ConstantAlpha:
            return {feedbackDamp, feedbackDamp, feedbackDamp, feedbackDamp};
        case BlendMode::Average:
        case BlendMode::Add:
        case BlendMode::Subtract:
        case BlendMode::AddQuarter:
            return {kQuarterBlendAlpha, kQuarterBlendAlpha, kQuarterBlendAlpha, kQuarterBlendAlpha};
        case BlendMode::None:
            break;
    }
    return {1.f, 1.f, 1.f, 1.f};
}

DepthStencilState primDepthStencil(const PrimPipelineKey& key) {
    DepthStencilState s;
    s.depthTest = key.depthTest;
    s.depthWrite = key.depthWrite;
    s.depthCompare = key.depthAlways ? CompareOp::Always : CompareOp::LessEqual;
    switch (key.stencil) {
        case StencilMode::Off:
            break;
        case StencilMode::MaskWrite:  // glStencilFunc(GL_ALWAYS, 1, 0x10); glStencilOp(REPLACE, REPLACE, REPLACE)
            s.stencilTest = true;
            s.stencil = {StencilOp::Replace, StencilOp::Replace, StencilOp::Replace, CompareOp::Always};
            s.stencilReadMask = 0x10;
            break;
        case StencilMode::MaskTest:  // glStencilFunc(GL_NOTEQUAL, 1, 0xFF); glStencilOp(REPLACE, KEEP, KEEP)
            s.stencilTest = true;
            s.stencil = {StencilOp::Replace, StencilOp::Keep, StencilOp::Keep, CompareOp::NotEqual};
            s.stencilReadMask = 0xFF;
            break;
    }
    return s;
}

PrimRenderer::PrimRenderer(Device& device, TextureFormat colorFormat, bool depthStencil)
    : device_(device), colorFormat_(colorFormat), depthStencil_(depthStencil) {}

PrimRenderer::~PrimRenderer() {
    for (auto& [key, pipeline] : pipelines_) device_.destroy(pipeline);
    device_.destroy(vertices_);
    device_.destroy(vs_);
    device_.destroy(fs_);
    device_.destroy(sampler_);
    device_.destroy(externalSampler_);
    device_.destroy(vram_);
    device_.destroy(fogRamp_);
}

Result<std::unique_ptr<PrimRenderer>> PrimRenderer::create(Device& device, TextureFormat colorFormat,
                                                                 bool depthStencil, std::uint32_t maxVertices) {
    using R = Result<std::unique_ptr<PrimRenderer>>;
    std::unique_ptr<PrimRenderer> self(new PrimRenderer(device, colorFormat, depthStencil));

    auto vram = device.createTexture({TextureFormat::RG8, kVramWidth, kVramHeight, kUsageSampled, "prim_vram"});
    if (!vram) return R::fail(vram.error());
    self->vram_ = vram.value();
    auto fogRamp = device.createTexture({TextureFormat::R8, kFogRampEntries, 1, kUsageSampled, "fog_ramp"});
    if (!fogRamp) return R::fail(fogRamp.error());
    self->fogRamp_ = fogRamp.value();

    // VRAM holds palette indices: hardware filtering would blend indices, so it is always nearest and the
    // shader filters after the CLUT lookup.
    auto sampler = device.createSampler({});
    if (!sampler) return R::fail(sampler.error());
    self->sampler_ = sampler.value();
    auto repeat = device.createSampler(
        {Filter::Nearest, Filter::Nearest, AddressMode::Repeat, AddressMode::Repeat});
    if (!repeat) return R::fail(repeat.error());
    self->externalSampler_ = repeat.value();

    const embedded::Shader vsSrc = embedded::primBatchVert();
    const embedded::Shader fsSrc = embedded::primBatchFrag();
    ShaderDesc vs;
    vs.stage = ShaderStage::Vertex;
    vs.glsl = vsSrc.glsl;
    vs.spirv = vsSrc.spirv;
    vs.uniformBlockNames = kVsBlocks;
    vs.name = "prim_batch.vert";
    auto vsH = device.createShader(vs);
    if (!vsH) return R::fail(vsH.error());
    self->vs_ = vsH.value();

    ShaderDesc fs;
    fs.stage = ShaderStage::Fragment;
    fs.glsl = fsSrc.glsl;
    fs.spirv = fsSrc.spirv;
    fs.samplerNames = kFsSamplers;
    fs.uniformBlockNames = kFsBlocks;
    fs.name = "prim_batch.frag";
    auto fsH = device.createShader(fs);
    if (!fsH) return R::fail(fsH.error());
    self->fs_ = fsH.value();

    auto vb = device.createBuffer(
        {BufferUsage::Vertex, static_cast<std::uint32_t>(maxVertices * sizeof(PrimVertex)), "prim_vertices"});
    if (!vb) return R::fail(vb.error());
    self->vertices_ = vb.value();
    self->maxVertices_ = maxVertices;
    return R::ok(std::move(self));
}

Result<void> PrimRenderer::uploadVertices(CommandList& cmd, std::span<const PrimVertex> vertices) {
    if (vertices.size() > maxVertices_) return Result<void>::fail("prim: vertex stream overflow");
    cmd.uploadBuffer(vertices_, 0, std::as_bytes(vertices));
    return Result<void>::ok();
}

void PrimRenderer::uploadFogRamp(CommandList& cmd, const FogRampUniforms& fog) {
    std::array<std::uint8_t, kFogRampEntries> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::uint8_t>(std::clamp(fog.ramp[i], 0.f, 255.f));
    if (fogRampUploaded_ && bytes == fogRampBytes_) return;
    cmd.uploadTexture(fogRamp_, {0, 0, static_cast<std::int32_t>(kFogRampEntries), 1}, std::as_bytes(std::span(bytes)));
    fogRampBytes_ = bytes;
    fogRampUploaded_ = true;
}

Result<PipelineH> PrimRenderer::pipeline(const PrimPipelineKey& key) {
    const std::uint32_t packed = key.pack();
    if (auto it = pipelines_.find(packed); it != pipelines_.end()) return Result<PipelineH>::ok(it->second);
    PipelineDesc desc;
    desc.vertexShader = vs_;
    desc.fragmentShader = fs_;
    desc.vertexLayout = {sizeof(PrimVertex), kAttributes};
    desc.blend = primBlendState(key.blend);
    desc.blend.alphaToCoverage = key.alphaToCoverage;
    desc.depthStencil = primDepthStencil(key);
    desc.colorFormat = colorFormat_;
    desc.hasDepthStencil = depthStencil_;
    desc.name = "prim_batch";
    auto p = device_.createPipeline(desc);
    if (!p) return p;
    pipelines_.emplace(packed, p.value());
    return p;
}

void PrimRenderer::beginDraws(RenderPass& pass, const PrimVsUniforms& vs, const PrimFsUniforms& fs) {
    pass.bindVertexBuffer(vertices_, 0);
    // Every slot the shader declares is bound (SDL3 GPU requires it); the RGBA and shadow slots get VRAM until
    // a caller binds its own.
    pass.bindSampler(ShaderStage::Fragment, kPrimSamplerVram, vram(), vramSampler());
    pass.bindSampler(ShaderStage::Fragment, kPrimSamplerRgba, vram(), vramSampler());
    pass.bindSampler(ShaderStage::Fragment, kPrimSamplerShadow, vram(), vramSampler());
    pass.bindSampler(ShaderStage::Fragment, kPrimSamplerFogRamp, fogRamp_, sampler_);
    pass.pushUniforms(ShaderStage::Vertex, 0, std::as_bytes(std::span(&vs, 1)));
    vsUniforms_ = vs;
    haveVs_ = true;
    fogUniforms_ = {};
    pass.pushUniforms(ShaderStage::Vertex, 1, std::as_bytes(std::span(&fogUniforms_, 1)));
    pass.pushUniforms(ShaderStage::Fragment, 1, std::as_bytes(std::span(&fogUniforms_, 1)));
    haveFog_ = true;
    fsUniforms_ = fs;
    haveFs_ = false;
}

Result<void> PrimRenderer::drawSplit(RenderPass& pass, const PrimPipelineKey& key, const PrimVsUniforms& vs,
                                        const PrimFsUniforms& fs, const FogRampUniforms& fog,
                                        std::uint32_t firstVertex, std::uint32_t count, float feedbackDamp) {
    auto p = pipeline(key);
    if (!p) return Result<void>::fail(p.error());
    pass.bindPipeline(p.value());
    pass.setBlendConstants(primBlendConstants(key.blend, feedbackDamp));
    pass.setStencilReference(kPrimStencilRef);
    if (!haveVs_ || !sameBytes(&vs, &vsUniforms_, sizeof vs)) {
        vsUniforms_ = vs;
        pass.pushUniforms(ShaderStage::Vertex, 0, std::as_bytes(std::span(&vsUniforms_, 1)));
        haveVs_ = true;
    }
    if (!haveFog_ || !sameBytes(&fog, &fogUniforms_, sizeof fog)) {
        fogUniforms_ = fog;
        pass.pushUniforms(ShaderStage::Vertex, 1, std::as_bytes(std::span(&fogUniforms_, 1)));
        pass.pushUniforms(ShaderStage::Fragment, 1, std::as_bytes(std::span(&fogUniforms_, 1)));
        haveFog_ = true;
    }
    PrimFsUniforms f = fs;
    f.modes[0] = fogsToBlack(key.blend) ? 1.f : 0.f;
    if (!haveFs_ || !sameBytes(&f, &fsUniforms_, sizeof f)) {
        fsUniforms_ = f;
        pass.pushUniforms(ShaderStage::Fragment, 0, std::as_bytes(std::span(&fsUniforms_, 1)));
        haveFs_ = true;
    }
    pass.draw(count, firstVertex);
    return Result<void>::ok();
}

Result<void> PrimRenderer::draw(RenderPass& pass, const PrimPipelineKey& key, std::uint32_t firstVertex,
                                   std::uint32_t count, float feedbackDamp) {
    auto p = pipeline(key);
    if (!p) return Result<void>::fail(p.error());
    pass.bindPipeline(p.value());
    pass.setBlendConstants(primBlendConstants(key.blend, feedbackDamp));
    pass.setStencilReference(kPrimStencilRef);
    const bool fogToBlack = fogsToBlack(key.blend);
    if (!haveFs_ || fogToBlack != lastFogToBlack_) {
        fsUniforms_.modes[0] = fogToBlack ? 1.f : 0.f;
        pass.pushUniforms(ShaderStage::Fragment, 0, std::as_bytes(std::span(&fsUniforms_, 1)));
        lastFogToBlack_ = fogToBlack;
        haveFs_ = true;
    }
    pass.draw(count, firstVertex);
    return Result<void>::ok();
}

}  // namespace sh::rhi
