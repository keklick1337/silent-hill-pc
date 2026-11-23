#include "media/fmv/fmv_renderer.h"

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

namespace sh::fmv {

namespace {

using namespace sh::rhi;

constexpr std::uint32_t kSamplerMovie = 0;
constexpr std::uint32_t kUniformMode = 0;

const std::string_view kFsSamplers[] = {"s_movie"};
const std::string_view kFsBlocks[] = {"FmvFs"};

struct QuadVertex {
    float x, y, u, v;
};

constexpr VertexAttribute kAttributes[] = {
    {0, VertexFormat::Float2, 0},
    {1, VertexFormat::Float2, 8},
};

// Two strips of four: the full-target bars quad, then the picture.
constexpr std::uint32_t kQuadVertices = 8;

struct alignas(16) ModeBlock {
    float mode[4];
};

}  // namespace

Result<std::unique_ptr<FmvRenderer>> FmvRenderer::create(Device& device, TextureFormat colorFormat) {
    using R = Result<std::unique_ptr<FmvRenderer>>;
    std::unique_ptr<FmvRenderer> r(new FmvRenderer(device));
    const auto vsrc = rhi::embedded::fmvBlitVert();
    const auto fsrc = rhi::embedded::fmvBlitFrag();
    if (spirvBackend(device.backend()) && (vsrc.spirv.empty() || fsrc.spirv.empty()))
        return R::fail("fmv shaders: no SPIR-V in this build");

    ShaderDesc vd;
    vd.stage = ShaderStage::Vertex;
    vd.glsl = vsrc.glsl;
    vd.spirv = vsrc.spirv;
    vd.name = "fmv_blit.vert";
    auto vs = device.createShader(vd);
    if (!vs) return R::fail(vs.error());
    r->vs_ = vs.value();

    ShaderDesc fd;
    fd.stage = ShaderStage::Fragment;
    fd.glsl = fsrc.glsl;
    fd.spirv = fsrc.spirv;
    fd.samplerNames = kFsSamplers;
    fd.uniformBlockNames = kFsBlocks;
    fd.name = "fmv_blit.frag";
    auto fs = device.createShader(fd);
    if (!fs) return R::fail(fs.error());
    r->fs_ = fs.value();

    PipelineDesc p;
    p.vertexShader = r->vs_;
    p.fragmentShader = r->fs_;
    p.vertexLayout = {sizeof(QuadVertex), kAttributes};
    p.primitive = PrimitiveType::TriangleStrip;
    p.colorFormat = colorFormat;
    p.name = "fmv";
    auto pipe = device.createPipeline(p);
    if (!pipe) return R::fail(pipe.error());
    r->pipeline_ = pipe.value();

    auto sampler = device.createSampler({Filter::Linear, Filter::Linear, AddressMode::ClampToEdge, AddressMode::ClampToEdge});
    if (!sampler) return R::fail(sampler.error());
    r->sampler_ = sampler.value();

    auto quad = device.createBuffer({BufferUsage::Vertex, kQuadVertices * sizeof(QuadVertex), "fmv quad"});
    if (!quad) return R::fail(quad.error());
    r->quad_ = quad.value();
    return R::ok(std::move(r));
}

FmvRenderer::~FmvRenderer() {
    if (frame_) device_.destroy(frame_);
    if (quad_) device_.destroy(quad_);
    if (sampler_) device_.destroy(sampler_);
    if (pipeline_) device_.destroy(pipeline_);
    if (fs_) device_.destroy(fs_);
    if (vs_) device_.destroy(vs_);
}

Result<void> FmvRenderer::upload(CommandList& cmd, const std::uint8_t* pixels, int width, int height, bool rgba,
                                 int targetW, int targetH) {
    if (!pixels || width <= 0 || height <= 0 || targetW <= 0 || targetH <= 0)
        return Result<void>::fail("fmv: empty frame or target");

    // Storage only changes with the frame size: a per-frame reallocation stalls some drivers hard at 4K.
    if (!frame_ || frameW_ != width || frameH_ != height) {
        if (frame_) device_.destroy(frame_);
        frame_ = {};
        frameW_ = frameH_ = 0;
        auto tex = device_.createTexture({TextureFormat::RGBA8, std::uint32_t(width), std::uint32_t(height),
                                          kUsageSampled, "fmv frame"});
        if (!tex) return Result<void>::fail("fmv frame " + std::to_string(width) + "x" + std::to_string(height) +
                                            ": " + tex.error());
        frame_ = tex.value();
        frameW_ = width;
        frameH_ = height;
    }

    const std::size_t texels = std::size_t(width) * std::size_t(height);
    std::span<const std::byte> bytes;
    if (rgba) {
        bytes = std::as_bytes(std::span(pixels, texels * 4));
    } else {
        // The RHI has no 3-byte format; the alpha it gains is the 1.0 an RGB texture samples as.
        rgbaScratch_.resize(texels * 4);
        std::uint8_t* d = rgbaScratch_.data();
        const std::uint8_t* s = pixels;
        for (std::size_t i = 0; i < texels; ++i, s += 3, d += 4) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 0xFF;
        }
        bytes = std::as_bytes(std::span(rgbaScratch_));
    }
    cmd.uploadTexture(frame_, {0, 0, width, height}, bytes);

    // Fit the movie's aspect inside the target.
    const float videoAspect = float(width) / float(height);
    const float targetAspect = float(targetW) / float(targetH);
    float sx = 1.0f, sy = 1.0f;
    if (videoAspect > targetAspect)
        sy = targetAspect / videoAspect;
    else
        sx = videoAspect / targetAspect;
    const std::array<QuadVertex, kQuadVertices> quad{{
        {-1.0f, 1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f, 1.0f},
        {1.0f, -1.0f, 1.0f, 1.0f},
        {-sx, sy, 0.0f, 0.0f},
        {sx, sy, 1.0f, 0.0f},
        {-sx, -sy, 0.0f, 1.0f},
        {sx, -sy, 1.0f, 1.0f},
    }};
    cmd.uploadBuffer(quad_, 0, std::as_bytes(std::span(quad)));
    targetW_ = targetW;
    targetH_ = targetH;
    return Result<void>::ok();
}

void FmvRenderer::draw(RenderPass& pass, bool setViewport) {
    if (!frame_) return;
    pass.bindPipeline(pipeline_);
    if (setViewport) {
        pass.setViewport({0.0f, 0.0f, float(targetW_), float(targetH_), 0.0f, 1.0f});
        pass.setScissor({0, 0, targetW_, targetH_});
    }
    pass.bindVertexBuffer(quad_, 0);
    pass.bindSampler(ShaderStage::Fragment, kSamplerMovie, frame_, sampler_);
    const ModeBlock bars{{0.0f, 0.0f, 0.0f, 0.0f}};
    pass.pushUniforms(ShaderStage::Fragment, kUniformMode, std::as_bytes(std::span(&bars, 1)));
    pass.draw(4, 0);
    const ModeBlock picture{{1.0f, 0.0f, 0.0f, 0.0f}};
    pass.pushUniforms(ShaderStage::Fragment, kUniformMode, std::as_bytes(std::span(&picture, 1)));
    pass.draw(4, 4);
}

void FmvRenderer::unbind(RenderPass& pass) {
    pass.bindSampler(ShaderStage::Fragment, kSamplerMovie, {}, {});
}

}  // namespace sh::fmv
