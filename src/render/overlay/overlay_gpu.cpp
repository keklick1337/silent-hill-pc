// The overlay's GPU side: one pipeline (source-over blend, no depth, no culling), a white texture for flat quads,
// RHI mirrors of the store's textures and one growing vertex buffer.
#include "render/overlay/overlay.h"

#include "render/rhi/shaders/embedded.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>

namespace sh::rhi::embedded {
Shader overlayVert();
Shader overlayFrag();
}  // namespace sh::rhi::embedded

namespace sh::overlay {

namespace {

constexpr std::uint32_t kSamplerSlot = 0;
constexpr std::uint32_t kUniformSlot = 0;
constexpr std::uint32_t kMinVertexBytes = 64u << 10;

const std::string_view kVsBlocks[] = {"OverlayVs"};
const std::string_view kFsSamplers[] = {"s_overlay"};
const std::string_view kFsBlocks[] = {"OverlayFs"};

constexpr rhi::VertexAttribute kAttributes[] = {
    {0, rhi::VertexFormat::Float2, 0},
    {1, rhi::VertexFormat::Float2, 8},
    {2, rhi::VertexFormat::UByte4Norm, 16},
};

struct alignas(16) Vec4Block {
    float v[4];
};

}  // namespace

class GpuRenderer {
public:
    static Result<std::unique_ptr<GpuRenderer>> create(rhi::Device& device);
    ~GpuRenderer();
    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;

    Result<void> prepare(rhi::CommandList& cmd, const TextureStore& store, const DrawList& list);
    // own_pass: the pass is the overlay's (viewport and scissor through it); otherwise `scissor` clips.
    void draw(rhi::RenderPass& pass, const DrawList& list, std::uint32_t w, std::uint32_t h, bool own_pass,
              Overlay::ScissorFn scissor, void* user);

private:
    explicit GpuRenderer(rhi::Device& device) : device_(device) {}

    struct Mirror {
        rhi::TextureH texture;
        std::uint64_t version = 0;
        std::int32_t w = 0, h = 0;
        Format format = Format::Rgba8;
        Filter filter = Filter::Nearest;
    };

    rhi::Device& device_;
    rhi::ShaderH vs_, fs_;
    rhi::PipelineH pipeline_;
    rhi::SamplerH nearest_, linear_;
    rhi::TextureH white_;
    bool whiteUploaded_ = false;
    rhi::BufferH vertices_;
    std::uint32_t vertexCapacity_ = 0;
    std::uint32_t vertexCount_ = 0;  // uploaded by the last prepare
    std::unordered_map<TextureId, Mirror> mirrors_;
};

Result<std::unique_ptr<GpuRenderer>> GpuRenderer::create(rhi::Device& device) {
    using R = Result<std::unique_ptr<GpuRenderer>>;
    std::unique_ptr<GpuRenderer> r(new GpuRenderer(device));
    const auto vsrc = rhi::embedded::overlayVert();
    const auto fsrc = rhi::embedded::overlayFrag();
    if (rhi::spirvBackend(device.backend()) && (vsrc.spirv.empty() || fsrc.spirv.empty()))
        return R::fail("overlay shaders: no SPIR-V in this build");

    rhi::ShaderDesc vd;
    vd.stage = rhi::ShaderStage::Vertex;
    vd.glsl = vsrc.glsl;
    vd.spirv = vsrc.spirv;
    vd.uniformBlockNames = kVsBlocks;
    vd.name = "overlay.vert";
    auto vs = device.createShader(vd);
    if (!vs) return R::fail(vs.error());
    r->vs_ = vs.value();

    rhi::ShaderDesc fd;
    fd.stage = rhi::ShaderStage::Fragment;
    fd.glsl = fsrc.glsl;
    fd.spirv = fsrc.spirv;
    fd.samplerNames = kFsSamplers;
    fd.uniformBlockNames = kFsBlocks;
    fd.name = "overlay.frag";
    auto fs = device.createShader(fd);
    if (!fs) return R::fail(fs.error());
    r->fs_ = fs.value();

    rhi::PipelineDesc p;
    p.vertexShader = r->vs_;
    p.fragmentShader = r->fs_;
    p.vertexLayout = {sizeof(Vertex), kAttributes};
    p.primitive = rhi::PrimitiveType::TriangleList;
    p.blend.enable = true;
    p.blend.srcColor = rhi::BlendFactor::SrcAlpha;
    p.blend.dstColor = rhi::BlendFactor::OneMinusSrcAlpha;
    p.blend.srcAlpha = rhi::BlendFactor::SrcAlpha;
    p.blend.dstAlpha = rhi::BlendFactor::OneMinusSrcAlpha;
    p.colorFormat = rhi::TextureFormat::RGBA8;
    p.name = "overlay";
    auto pipe = device.createPipeline(p);
    if (!pipe) return R::fail(pipe.error());
    r->pipeline_ = pipe.value();

    auto nearest = device.createSampler({rhi::Filter::Nearest, rhi::Filter::Nearest, rhi::AddressMode::ClampToEdge,
                                         rhi::AddressMode::ClampToEdge});
    if (!nearest) return R::fail(nearest.error());
    r->nearest_ = nearest.value();
    auto linear = device.createSampler({rhi::Filter::Linear, rhi::Filter::Linear, rhi::AddressMode::ClampToEdge,
                                        rhi::AddressMode::ClampToEdge});
    if (!linear) return R::fail(linear.error());
    r->linear_ = linear.value();

    auto white = device.createTexture({rhi::TextureFormat::RGBA8, 1, 1, rhi::kUsageSampled, "overlay white"});
    if (!white) return R::fail(white.error());
    r->white_ = white.value();
    return R::ok(std::move(r));
}

GpuRenderer::~GpuRenderer() {
    for (auto& [id, m] : mirrors_)
        if (m.texture) device_.destroy(m.texture);
    if (vertices_) device_.destroy(vertices_);
    if (white_) device_.destroy(white_);
    if (linear_) device_.destroy(linear_);
    if (nearest_) device_.destroy(nearest_);
    if (pipeline_) device_.destroy(pipeline_);
    if (fs_) device_.destroy(fs_);
    if (vs_) device_.destroy(vs_);
}

Result<void> GpuRenderer::prepare(rhi::CommandList& cmd, const TextureStore& store, const DrawList& list) {
    if (!whiteUploaded_) {
        const std::byte one[4] = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
        cmd.uploadTexture(white_, {0, 0, 1, 1}, one);
        whiteUploaded_ = true;
    }

    // Mirrors of destroyed textures go first, so a long session does not keep every baked label alive.
    for (auto it = mirrors_.begin(); it != mirrors_.end();) {
        if (!store.find(it->first)) {
            if (it->second.texture) device_.destroy(it->second.texture);
            it = mirrors_.erase(it);
        } else {
            ++it;
        }
    }

    std::unordered_set<TextureId> synced;
    for (const Batch& b : list.batches()) {
        if (!b.texture || !synced.insert(b.texture).second) continue;
        const TextureStore::Texture* t = store.find(b.texture);
        if (!t) continue;
        Mirror& m = mirrors_[b.texture];
        if (m.texture && m.version == t->version) continue;
        if (!m.texture || m.w != t->w || m.h != t->h || m.format != t->format) {
            if (m.texture) device_.destroy(m.texture);
            m.texture = {};
            auto tex = device_.createTexture({t->format == Format::Alpha8 ? rhi::TextureFormat::R8
                                                                          : rhi::TextureFormat::RGBA8,
                                              std::uint32_t(t->w), std::uint32_t(t->h), rhi::kUsageSampled,
                                              "overlay texture"});
            if (!tex) {
                mirrors_.erase(b.texture);
                return Result<void>::fail("overlay texture " + std::to_string(t->w) + "x" + std::to_string(t->h) +
                                          ": " + tex.error());
            }
            m.texture = tex.value();
            m.w = t->w;
            m.h = t->h;
            m.format = t->format;
        }
        m.filter = t->filter;
        cmd.uploadTexture(m.texture, {0, 0, t->w, t->h}, std::as_bytes(std::span(t->pixels)));
        m.version = t->version;
    }

    const auto bytes = static_cast<std::uint32_t>(list.vertices().size() * sizeof(Vertex));
    vertexCount_ = 0;
    if (bytes == 0) return Result<void>::ok();
    if (bytes > vertexCapacity_) {
        if (vertices_) device_.destroy(vertices_);
        vertices_ = {};
        vertexCapacity_ = 0;
        std::uint32_t cap = kMinVertexBytes;
        while (cap < bytes) cap <<= 1;
        auto buf = device_.createBuffer({rhi::BufferUsage::Vertex, cap, "overlay vertices"});
        if (!buf) return Result<void>::fail("overlay vertices: " + buf.error());
        vertices_ = buf.value();
        vertexCapacity_ = cap;
    }
    cmd.uploadBuffer(vertices_, 0, std::as_bytes(std::span(list.vertices())));
    vertexCount_ = static_cast<std::uint32_t>(list.vertices().size());
    return Result<void>::ok();
}

void GpuRenderer::draw(rhi::RenderPass& pass, const DrawList& list, std::uint32_t w, std::uint32_t h, bool own_pass,
                       Overlay::ScissorFn scissor, void* user) {
    if (list.empty() || vertexCount_ != list.vertices().size() || w == 0 || h == 0) return;
    const rhi::Rect full{0, 0, std::int32_t(w), std::int32_t(h)};
    pass.bindPipeline(pipeline_);
    if (own_pass) {
        pass.setViewport({0.f, 0.f, float(w), float(h), 0.f, 1.f});
        pass.setScissor(full);
    }
    pass.bindVertexBuffer(vertices_, 0);
    const Vec4Block target{{float(w), float(h), 0.f, 0.f}};
    pass.pushUniforms(rhi::ShaderStage::Vertex, kUniformSlot, std::as_bytes(std::span(&target, 1)));

    bool clipped = false;
    PixelRect clip;
    for (const Batch& b : list.batches()) {
        if (b.scissored != clipped || (b.scissored && !(b.scissor == clip))) {
            clipped = b.scissored;
            clip = b.scissor;
            if (own_pass)
                pass.setScissor(clipped ? rhi::Rect{clip.x, clip.y, clip.w, clip.h} : full);
            else if (scissor)
                scissor(user, clipped ? &clip : nullptr);
        }
        rhi::TextureH tex = white_;
        bool linear = false, alpha = false;
        if (b.texture) {
            auto it = mirrors_.find(b.texture);
            if (it == mirrors_.end() || !it->second.texture) continue;  // destroyed before it was ever uploaded
            tex = it->second.texture;
            linear = it->second.filter == Filter::Linear;
            alpha = it->second.format == Format::Alpha8;
        }
        pass.bindSampler(rhi::ShaderStage::Fragment, kSamplerSlot, tex, linear ? linear_ : nearest_);
        const Vec4Block mode{{alpha ? 1.f : 0.f, 0.f, 0.f, 0.f}};
        pass.pushUniforms(rhi::ShaderStage::Fragment, kUniformSlot, std::as_bytes(std::span(&mode, 1)));
        pass.draw(b.count, b.first);
    }
    if (clipped && !own_pass && scissor) scissor(user, nullptr);
    // Foreign GL code sharing the context must never sample through the overlay's sampler object.
    pass.bindSampler(rhi::ShaderStage::Fragment, kSamplerSlot, {}, {});
}

// ---- Overlay's GPU entry points ---------------------------------------------------------------------------

Overlay::Overlay() = default;
Overlay::~Overlay() = default;

void Overlay::release_gpu() {
    gpu_.reset();
    gpuDevice_ = nullptr;
}

Result<void> Overlay::prepare(rhi::Device& device, rhi::CommandList& cmd) {
    if (gpuDevice_ != &device) release_gpu();
    if (!gpu_) {
        auto r = GpuRenderer::create(device);
        if (!r) return Result<void>::fail(r.error());
        gpu_ = std::move(r.value());
        gpuDevice_ = &device;
    }
    return gpu_->prepare(cmd, textures_, list_);
}

void Overlay::draw(rhi::RenderPass& pass, std::uint32_t w, std::uint32_t h, ScissorFn scissor, void* user) {
    if (gpu_) gpu_->draw(pass, list_, w, h, false, scissor, user);
}

Result<void> Overlay::record(rhi::Device& device, rhi::CommandList& cmd, rhi::TextureH target, std::uint32_t w,
                             std::uint32_t h) {
    if (list_.empty() || !target || w == 0 || h == 0) return Result<void>::ok();
    auto prep = prepare(device, cmd);
    if (!prep) return prep;
    rhi::RenderPassDesc rp;
    rp.color = {target, rhi::LoadOp::Load, rhi::StoreOp::Store, {0.f, 0.f, 0.f, 0.f}};
    rp.name = "overlay";
    rhi::RenderPass* pass = cmd.beginRenderPass(rp);
    if (!pass) return Result<void>::fail("overlay: render pass did not begin");
    gpu_->draw(*pass, list_, w, h, true, nullptr, nullptr);
    cmd.endRenderPass();
    return Result<void>::ok();
}

}  // namespace sh::overlay
