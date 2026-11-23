// The registry of RHI textures (render/gpu/textures): ids, resolve, destroy reuse, sizes, halving on a refused
// allocation, the nearest flag against the preset filter.

#include "framework/sh_test.h"
#include "render/gpu/textures.h"

#include <cstdint>
#include <map>
#include <vector>

namespace {

namespace rhi = sh::rhi;
namespace gpu = sh::gpu;

struct FakeTexture {
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint8_t> texels;
};

class FakeList final : public rhi::CommandList {
public:
    explicit FakeList(std::map<std::uint32_t, FakeTexture>& t) : textures_(t) {}
    void uploadTexture(rhi::TextureH texture, const rhi::Rect& rect, std::span<const std::byte> pixels) override {
        FakeTexture& t = textures_.at(texture.raw);
        for (int y = 0; y < rect.h; ++y)
            for (int x = 0; x < rect.w * 4; ++x)
                t.texels[((rect.y + y) * t.w * 4) + rect.x * 4 + x] =
                    static_cast<std::uint8_t>(pixels[static_cast<std::size_t>(y * rect.w * 4 + x)]);
        ++uploads;
    }
    void uploadBuffer(rhi::BufferH, std::uint32_t, std::span<const std::byte>) override {}
    void copyTexture(rhi::TextureH, const rhi::Rect&, rhi::TextureH, std::int32_t, std::int32_t) override {}
    void blitTexture(rhi::TextureH, const rhi::Rect&, rhi::TextureH, const rhi::Rect&, rhi::Filter) override {}
    void resolveTexture(rhi::TextureH, rhi::TextureH) override {}
    rhi::RenderPass* beginRenderPass(const rhi::RenderPassDesc&) override { return nullptr; }
    void endRenderPass() override {}
    rhi::TextureH acquireSwapchainTexture(std::uint32_t*, std::uint32_t*) override { return {}; }
    int uploads = 0;

private:
    std::map<std::uint32_t, FakeTexture>& textures_;
};

class FakeDevice final : public rhi::Device {
public:
    rhi::Backend backend() const override { return rhi::Backend::Gl33; }
    const rhi::Caps& caps() const override { return caps_; }
    sh::Result<rhi::TextureH> createTexture(const rhi::TextureDesc& d) override {
        if (d.width > limit || d.height > limit) return sh::Result<rhi::TextureH>::fail("too big");
        const rhi::TextureH h{next_++};
        textures[h.raw] = FakeTexture{d.width, d.height, std::vector<std::uint8_t>(d.width * d.height * 4, 0xCD)};
        return sh::Result<rhi::TextureH>::ok(h);
    }
    sh::Result<rhi::BufferH> createBuffer(const rhi::BufferDesc&) override { return sh::Result<rhi::BufferH>::fail("-"); }
    sh::Result<rhi::SamplerH> createSampler(const rhi::SamplerDesc& d) override {
        samplers.push_back(d);
        return sh::Result<rhi::SamplerH>::ok(rhi::SamplerH{static_cast<std::uint32_t>(samplers.size())});
    }
    sh::Result<rhi::ShaderH> createShader(const rhi::ShaderDesc&) override { return sh::Result<rhi::ShaderH>::fail("-"); }
    sh::Result<rhi::PipelineH> createPipeline(const rhi::PipelineDesc&) override {
        return sh::Result<rhi::PipelineH>::fail("-");
    }
    void destroy(rhi::TextureH h) override { textures.erase(h.raw); }
    void destroy(rhi::BufferH) override {}
    void destroy(rhi::SamplerH) override {}
    void destroy(rhi::ShaderH) override {}
    void destroy(rhi::PipelineH) override {}
    rhi::CommandList* begin() override { return &list; }
    sh::Result<void> submit() override { return sh::Result<void>::ok(); }
    sh::Result<void> readback(rhi::TextureH, const rhi::Rect&, std::span<std::byte>) override {
        return sh::Result<void>::fail("-");
    }
    void waitIdle() override {}

    std::uint32_t limit = 1u << 20;
    std::map<std::uint32_t, FakeTexture> textures;
    std::vector<rhi::SamplerDesc> samplers;
    FakeList list{textures};

private:
    rhi::Caps caps_;
    std::uint32_t next_ = 100;
};

std::vector<std::uint8_t> solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    std::vector<std::uint8_t> px;
    for (int i = 0; i < w * h; ++i) px.insert(px.end(), {r, g, b, a});
    return px;
}

}  // namespace

SH_TEST(gpu_textures_white_and_ids) {
    FakeDevice dev;
    gpu::Textures tex(dev);
    SH_CHECK(static_cast<bool>(tex.white()));
    tex.flush(dev.list);
    const FakeTexture& w = dev.textures.at(tex.white().raw);
    SH_CHECK(w.w == 1 && w.h == 1);
    SH_CHECK(w.texels == std::vector<std::uint8_t>({0xFF, 0xFF, 0xFF, 0xFF}));
    SH_CHECK(tex.resolve(gpu::TextureRef{}) == tex.white());

    const auto px = solid(3, 5, 1, 2, 3, 4);
    const std::uint32_t a = tex.create_rgba(3, 5, px, "a");
    const std::uint32_t b = tex.create_rgba(3, 5, px, "b");
    SH_CHECK(a != 0 && b != 0 && a != b);
    SH_CHECK(tex.handle(a) != tex.handle(b));
    SH_CHECK(!tex.handle(0));
    SH_CHECK(!tex.handle(9999));

    gpu::TextureRef ref;
    ref.kind = gpu::TextureRef::Kind::Override;
    ref.id = a;
    SH_CHECK(tex.resolve(ref) == tex.handle(a));
    ref.kind = gpu::TextureRef::Kind::Feedback;
    ref.id = b;
    SH_CHECK(tex.resolve(ref) == tex.handle(b));
    ref.kind = gpu::TextureRef::Kind::Vram;
    SH_CHECK(!tex.resolve(ref));
    ref.kind = gpu::TextureRef::Kind::Override;
    ref.id = 4242;
    SH_CHECK(!tex.resolve(ref));
    SH_CHECK(tex.create_rgba(0, 4, {}, "empty") == 0);
    SH_CHECK(tex.create_rgba(4, 4, std::span<const std::uint8_t>(px.data(), 8), "short") == 0);
}

SH_TEST(gpu_textures_upload_and_size) {
    FakeDevice dev;
    gpu::Textures tex(dev);
    tex.flush(dev.list);
    const auto px = solid(7, 3, 10, 20, 30, 40);  // non-power-of-two
    const std::uint32_t id = tex.create_rgba(7, 3, px, "npot");
    int w = 0, h = 0;
    SH_CHECK(tex.size(id, &w, &h) && w == 7 && h == 3);
    SH_CHECK(tex.pending_uploads() == 1);
    tex.flush(dev.list);
    SH_CHECK(tex.pending_uploads() == 0);
    SH_CHECK(dev.textures.at(tex.handle(id).raw).texels == px);

    const auto red = solid(2, 2, 255, 0, 0, 255);
    tex.update_rgba(id, rhi::Rect{5, 1, 2, 2}, red);
    tex.flush(dev.list);
    const auto& t = dev.textures.at(tex.handle(id).raw).texels;
    SH_CHECK(t[(1 * 7 + 5) * 4] == 255 && t[(2 * 7 + 6) * 4 + 1] == 0);
    SH_CHECK(t[(1 * 7 + 4) * 4] == 10);

    // Created without data: transparent, not undefined.
    const std::uint32_t blank = tex.create_rgba(2, 2, {}, "blank");
    tex.flush(dev.list);
    SH_CHECK(dev.textures.at(tex.handle(blank).raw).texels == std::vector<std::uint8_t>(16, 0));
}

SH_TEST(gpu_textures_destroy_reuses_ids) {
    FakeDevice dev;
    gpu::Textures tex(dev);
    const auto px = solid(2, 2, 0, 0, 0, 255);
    const std::uint32_t a = tex.create_rgba(2, 2, px, "a");
    const std::uint32_t b = tex.create_rgba(2, 2, px, "b");
    const rhi::TextureH ha = tex.handle(a);
    const std::size_t live = dev.textures.size();
    tex.destroy(a);
    SH_CHECK(dev.textures.size() == live - 1);
    SH_CHECK(!tex.handle(a));
    SH_CHECK(!tex.size(a, nullptr, nullptr));
    // Its queued upload went with it: flushing must not touch a released texture.
    tex.flush(dev.list);
    SH_CHECK(dev.textures.count(ha.raw) == 0);
    tex.destroy(a);  // twice is harmless
    tex.destroy(0);
    const std::uint32_t c = tex.create_rgba(2, 2, px, "c");
    SH_CHECK(c == a);
    SH_CHECK(c != b && tex.handle(c) != tex.handle(b));
    // The white texture stays for the renderer's lifetime.
    const rhi::TextureH white = tex.white();
    for (std::uint32_t id = 1; id < 8; ++id)
        if (tex.handle(id) == white) tex.destroy(id);
    SH_CHECK(tex.white() == white);
}

SH_TEST(gpu_textures_halving_on_failure) {
    FakeDevice dev;
    gpu::Textures tex(dev);
    tex.flush(dev.list);
    dev.limit = 100;
    // 300x40 does not fit; 150x20 does not; 75x10 does.
    std::vector<std::uint8_t> px;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 300; ++x) px.insert(px.end(), {static_cast<std::uint8_t>(x < 150 ? 0 : 200), 0, 0, 255});
    const std::uint32_t id = tex.create_rgba(300, 40, px, "big");
    SH_CHECK(id != 0);
    int w = 0, h = 0;
    SH_CHECK(tex.size(id, &w, &h) && w == 75 && h == 10);
    tex.flush(dev.list);
    const auto& t = dev.textures.at(tex.handle(id).raw).texels;
    SH_CHECK(t.size() == 75u * 10 * 4);
    SH_CHECK(t[0] == 0 && t[74 * 4] == 200 && t[3] == 255);

    // A full-size update follows the halving.
    tex.update_rgba(id, rhi::Rect{0, 0, 300, 40}, solid(300, 40, 9, 9, 9, 9));
    tex.flush(dev.list);
    SH_CHECK(dev.textures.at(tex.handle(id).raw).texels == solid(75, 10, 9, 9, 9, 9));

    // The configured limit scales before the device is asked.
    dev.limit = 1u << 20;
    tex.set_max_size(64);
    const std::uint32_t lim = tex.create_rgba(256, 32, solid(256, 32, 1, 1, 1, 1), "limited");
    SH_CHECK(tex.size(lim, &w, &h) && w == 64 && h == 8);

    // Nothing fits: no id.
    dev.limit = 0;
    SH_CHECK(tex.create_rgba(4, 4, solid(4, 4, 0, 0, 0, 0), "never") == 0);
}

SH_TEST(gpu_textures_nearest_flag) {
    FakeDevice dev;
    gpu::Textures tex(dev);
    SH_CHECK(dev.samplers.size() == 2);
    const std::uint32_t id = tex.create_rgba(2, 2, solid(2, 2, 0, 0, 0, 0), "t");
    const rhi::SamplerH point = tex.nearest_sampler();
    auto filter_of = [&](rhi::SamplerH s) { return dev.samplers.at(s.raw - 1).magFilter; };

    SH_CHECK(tex.sampler(id) == point);  // preset off
    tex.set_filter(1);
    SH_CHECK(filter_of(tex.sampler(id)) == rhi::Filter::Linear);
    SH_CHECK(dev.samplers.at(tex.sampler(id).raw - 1).addressU == rhi::AddressMode::ClampToEdge);
    tex.set_nearest(id, true);
    SH_CHECK(tex.nearest(id));
    SH_CHECK(tex.sampler(id) == point);
    tex.set_filter(3);
    SH_CHECK(tex.sampler(id) == point);
    tex.set_nearest(id, false);
    SH_CHECK(filter_of(tex.sampler(id)) == rhi::Filter::Linear);
    tex.set_filter(0);
    SH_CHECK(tex.sampler(id) == point);
    SH_CHECK(filter_of(point) == rhi::Filter::Nearest);

    // A reused id does not inherit the flag of the texture it replaces.
    tex.set_nearest(id, true);
    tex.destroy(id);
    const std::uint32_t again = tex.create_rgba(2, 2, solid(2, 2, 0, 0, 0, 0), "again");
    SH_CHECK(again == id && !tex.nearest(again));
}
