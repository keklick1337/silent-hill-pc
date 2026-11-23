// Textures of the engine's scene (glTF characters, map cells, mod replacements): each one is its own device texture
// at its own size. An oversized replacement of one (an HD HERO.png) must not touch the others, and above the device
// limit it is scaled down instead of failing.

#include "framework/sh_test.h"
#include "render/rhi/rhi.h"
#include "render/scene/area_cells.h"
#include "render/scene/scene.h"
#include "render/scene/scene_c.h"

#include <cstring>
#include <map>
#include <vector>

namespace {

namespace sc = sh::scene;
using namespace sh::rhi;
using sh::Result;

// Records what the scene asks of a device; no GPU.
class RecordingDevice final : public Device, public CommandList, public RenderPass {
public:
    struct Tex {
        TextureDesc desc;
        std::vector<std::byte> texels;  // RGBA8, as uploaded
        int uploads = 0;
    };
    std::map<std::uint32_t, Tex> textures;
    Caps caps_info;
    std::uint32_t next = 1;
    std::uint32_t silent_limit = 0;  // refused above this without being reported in caps
    int out_of_bounds_uploads = 0;
    int refuse_next = 0;  // this many createTexture calls fail whatever the size (a transient device failure)
    int creates = 0;

    Backend backend() const override { return Backend::Gl33; }
    const Caps& caps() const override { return caps_info; }
    Result<TextureH> createTexture(const TextureDesc& desc) override {
        ++creates;
        if (refuse_next > 0) {
            --refuse_next;
            return Result<TextureH>::fail("transient");
        }
        if (desc.width == 0 || desc.height == 0) return Result<TextureH>::fail("empty");
        if (caps_info.maxTextureSize && (desc.width > caps_info.maxTextureSize || desc.height > caps_info.maxTextureSize))
            return Result<TextureH>::fail("over the limit");
        if (silent_limit && (desc.width > silent_limit || desc.height > silent_limit))
            return Result<TextureH>::fail("out of memory");
        Tex t;
        t.desc = desc;
        t.desc.name = {};
        t.texels.assign(static_cast<std::size_t>(desc.width) * desc.height * 4, std::byte{0});
        const std::uint32_t id = next++;
        textures.emplace(id, std::move(t));
        return Result<TextureH>::ok(TextureH{id});
    }
    Result<BufferH> createBuffer(const BufferDesc&) override { return Result<BufferH>::ok(BufferH{next++}); }
    Result<SamplerH> createSampler(const SamplerDesc&) override { return Result<SamplerH>::ok(SamplerH{next++}); }
    Result<ShaderH> createShader(const ShaderDesc&) override { return Result<ShaderH>::ok(ShaderH{next++}); }
    Result<PipelineH> createPipeline(const PipelineDesc&) override { return Result<PipelineH>::ok(PipelineH{next++}); }
    void destroy(TextureH h) override { textures.erase(h.raw); }
    void destroy(BufferH) override {}
    void destroy(SamplerH) override {}
    void destroy(ShaderH) override {}
    void destroy(PipelineH) override {}
    CommandList* begin() override { return this; }
    Result<void> submit() override { return Result<void>::ok(); }
    Result<void> readback(TextureH, const Rect&, std::span<std::byte>) override { return Result<void>::ok(); }
    void waitIdle() override {}

    void uploadTexture(TextureH texture, const Rect& rect, std::span<const std::byte> pixels) override {
        auto it = textures.find(texture.raw);
        if (it == textures.end()) {
            ++out_of_bounds_uploads;
            return;
        }
        Tex& t = it->second;
        const auto w = static_cast<std::int32_t>(t.desc.width), h = static_cast<std::int32_t>(t.desc.height);
        if (rect.x < 0 || rect.y < 0 || rect.w <= 0 || rect.h <= 0 || rect.x + rect.w > w || rect.y + rect.h > h ||
            pixels.size() < static_cast<std::size_t>(rect.w) * rect.h * 4) {
            ++out_of_bounds_uploads;
            return;
        }
        for (std::int32_t y = 0; y < rect.h; ++y)
            std::memcpy(&t.texels[(static_cast<std::size_t>(rect.y + y) * w + rect.x) * 4],
                        &pixels[static_cast<std::size_t>(y) * rect.w * 4], static_cast<std::size_t>(rect.w) * 4);
        ++t.uploads;
    }
    void uploadBuffer(BufferH, std::uint32_t, std::span<const std::byte>) override {}
    void copyTexture(TextureH, const Rect&, TextureH, std::int32_t, std::int32_t) override {}
    void blitTexture(TextureH, const Rect&, TextureH, const Rect&, Filter) override {}
    void resolveTexture(TextureH, TextureH) override {}
    RenderPass* beginRenderPass(const RenderPassDesc&) override { return this; }
    void endRenderPass() override {}
    TextureH acquireSwapchainTexture(std::uint32_t*, std::uint32_t*) override { return {}; }

    void bindPipeline(PipelineH) override {}
    void setViewport(const Viewport&) override {}
    void setScissor(const Rect&) override {}
    void setBlendConstants(const std::array<float, 4>&) override {}
    void setStencilReference(std::uint8_t) override {}
    void bindVertexBuffer(BufferH, std::uint32_t) override {}
    void bindIndexBuffer(BufferH, IndexType, std::uint32_t) override {}
    void bindSampler(ShaderStage, std::uint32_t, TextureH, SamplerH) override {}
    void pushUniforms(ShaderStage, std::uint32_t, std::span<const std::byte>) override {}
    void draw(std::uint32_t, std::uint32_t) override {}
    void drawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}

    const Tex* find(int width, int height) const {
        for (const auto& [id, t] : textures)
            if (t.desc.width == static_cast<std::uint32_t>(width) && t.desc.height == static_cast<std::uint32_t>(height))
                return &t;
        return nullptr;
    }
};

std::vector<std::uint8_t> pattern(int w, int h, std::uint8_t seed) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i] = static_cast<std::uint8_t>(seed + i / 4);
        px[i + 1] = static_cast<std::uint8_t>(seed * 3 + i / 7);
        px[i + 2] = seed;
        px[i + 3] = 255;
    }
    return px;
}

// One posed mesh (a triangle) using `key`'s texture, registered through the game's C face.
void add_mesh(std::uint64_t key, int w, int h, const std::vector<std::uint8_t>& rgba) {
    ShScenePrelitVertex v[3] = {};
    for (int i = 0; i < 3; ++i) {
        v[i].x = static_cast<float>(i);
        v[i].z = 100.f;
        v[i].a = 255;
    }
    const unsigned idx[3] = {0, 1, 2};
    ShScenePrelit m;
    std::memset(&m, 0, sizeof m);
    m.textureKey = key;
    m.rgba = rgba.data();
    m.textureWidth = w;
    m.textureHeight = h;
    m.vertices = v;
    m.vertexCount = 3;
    m.indices = idx;
    m.indexCount = 3;
    SH_CHECK(ShScene_AddPrelit(&m) >= 0);
}

bool same(const RecordingDevice::Tex* t, const std::vector<std::uint8_t>& rgba) {
    return t != nullptr && t->uploads == 1 && t->texels.size() == rgba.size() &&
           std::memcmp(t->texels.data(), rgba.data(), rgba.size()) == 0;
}

}  // namespace

SH_TEST(scene_fit_texture_keeps_what_fits) {
    sc::SceneTexture t;
    t.width = 1448;
    t.height = 1086;
    t.rgba = pattern(t.width, t.height, 7);
    const sc::SceneTexture same_size = sc::fit_texture(t, 4096);
    SH_CHECK(same_size.width == 1448 && same_size.height == 1086 && same_size.rgba == t.rgba);
    SH_CHECK(sc::fit_texture(t, 0).rgba == t.rgba);
}

SH_TEST(scene_fit_texture_scales_down_keeping_aspect_and_alpha_classes) {
    sc::SceneTexture t;
    t.width = 8192;
    t.height = 6144;
    t.rgba.assign(static_cast<std::size_t>(t.width) * t.height * 4, 0);
    for (std::size_t i = 0; i < t.rgba.size(); i += 4) {
        const std::size_t x = (i / 4) % t.width;
        t.rgba[i] = 200;
        t.rgba[i + 1] = 100;
        t.rgba[i + 2] = 50;
        t.rgba[i + 3] = x < 4096 ? 255 : 0;  // left half solid, right half cut out
    }
    const sc::SceneTexture f = sc::fit_texture(t, 4096);
    SH_CHECK(f.width == 4096 && f.height == 3072);
    SH_CHECK(f.rgba.size() == static_cast<std::size_t>(4096) * 3072 * 4);
    SH_CHECK(f.rgba[0] == 200 && f.rgba[1] == 100 && f.rgba[2] == 50 && f.rgba[3] == 255);
    const std::size_t right = (static_cast<std::size_t>(100) * 4096 + 3000) * 4;
    SH_CHECK(f.rgba[right + 3] == 0);
}

// HERO.png replaced by a 1448x1086 picture (non power of two). Every texture stays its own
// device texture at its own size, the others byte-identical to their pictures; nothing is written outside a
// texture's own rectangle and no 1024x512 VRAM-like page or shared atlas is made.
SH_TEST(scene_oversized_replacement_leaves_other_textures_alone) {
    ShScene_FrameEnd();
    const std::uint64_t hero_key = 0x1001, cybil_key = 0x2001, tiny_key = 0x3001;
    const auto hero = pattern(1448, 1086, 11);
    const auto cybil = pattern(256, 192, 99);
    const auto tiny = pattern(3, 5, 42);
    add_mesh(cybil_key, 256, 192, cybil);
    add_mesh(hero_key, 1448, 1086, hero);
    add_mesh(tiny_key, 3, 5, tiny);

    RecordingDevice dev;
    dev.caps_info.maxTextureSize = 16384;
    auto r = sc::SceneRenderer::create(dev, TextureFormat::RGBA8);
    SH_CHECK(r);
    if (!r) return;
    SH_CHECK(r.value()->prepare(dev, sc::current_frame()));

    SH_CHECK(dev.out_of_bounds_uploads == 0);
    SH_CHECK(same(dev.find(1448, 1086), hero));
    SH_CHECK(same(dev.find(256, 192), cybil));
    SH_CHECK(same(dev.find(3, 5), tiny));
    SH_CHECK(dev.find(1024, 512) == nullptr);
    ShScene_FrameEnd();
}

// Above the device limit the picture is scaled down to it; the other textures are untouched.
SH_TEST(scene_texture_over_device_limit_is_scaled_not_dropped) {
    ShScene_FrameEnd();
    const std::uint64_t big_key = 0x4001, other_key = 0x5001;
    std::vector<std::uint8_t> big(static_cast<std::size_t>(4096) * 3072 * 4, 0);
    for (std::size_t i = 0; i < big.size(); i += 4) {
        big[i] = 10;
        big[i + 1] = 20;
        big[i + 2] = 30;
        big[i + 3] = 255;
    }
    const auto other = pattern(512, 256, 5);
    add_mesh(big_key, 4096, 3072, big);
    add_mesh(other_key, 512, 256, other);

    RecordingDevice dev;
    dev.caps_info.maxTextureSize = 2048;
    auto r = sc::SceneRenderer::create(dev, TextureFormat::RGBA8);
    SH_CHECK(r);
    if (!r) return;
    SH_CHECK(r.value()->prepare(dev, sc::current_frame()));
    SH_CHECK(dev.out_of_bounds_uploads == 0);
    const RecordingDevice::Tex* scaled = dev.find(2048, 1536);
    SH_CHECK(scaled != nullptr && scaled->uploads == 1);
    if (scaled != nullptr) SH_CHECK(scaled->texels[0] == std::byte{10} && scaled->texels[3] == std::byte{255});
    SH_CHECK(same(dev.find(512, 256), other));

    // A backend that reports no limit and refuses the size: halved until it takes it.
    RecordingDevice quiet;
    quiet.silent_limit = 1500;
    auto q = sc::SceneRenderer::create(quiet, TextureFormat::RGBA8);
    SH_CHECK(q);
    if (!q) return;
    SH_CHECK(q.value()->prepare(quiet, sc::current_frame()));
    SH_CHECK(quiet.out_of_bounds_uploads == 0);
    const RecordingDevice::Tex* halved = quiet.find(1024, 768);
    SH_CHECK(halved != nullptr && halved->uploads == 1);
    SH_CHECK(same(quiet.find(512, 256), other));
    ShScene_FrameEnd();
}

// The cutscene -> gameplay cut: the first gameplay frame shows textures the cutscene never drew (another material of
// Harry, the cells behind the new camera). If the device refuses those uploads at that moment (a pending error,
// memory), they must not be remembered as missing and drawn untextured for the rest of the session: they are
// tried again, at their own full size, and nothing already on the device is touched or re-uploaded.
SH_TEST(scene_texture_refused_at_the_cut_is_retried_and_others_kept) {
    ShScene_FrameEnd();
    const std::uint64_t hero_key = 0x6001, cybil_key = 0x7001, hero_alt_key = 0x6002;
    const auto hero = pattern(1448, 1086, 21);
    const auto cybil = pattern(256, 192, 77);
    const auto hero_alt = pattern(256, 192, 140);

    RecordingDevice dev;
    dev.caps_info.maxTextureSize = 16384;
    auto r = sc::SceneRenderer::create(dev, TextureFormat::RGBA8);
    SH_CHECK(r);
    if (!r) return;
    sc::SceneRenderer& scene = *r.value();

    // Cutscene frames: Harry (HD) and Cybil.
    for (int f = 0; f < 3; ++f) {
        add_mesh(hero_key, 1448, 1086, hero);
        add_mesh(cybil_key, 256, 192, cybil);
        SH_CHECK(scene.prepare(dev, sc::current_frame()));
        ShScene_FrameEnd();
    }
    SH_CHECK(same(dev.find(1448, 1086), hero));
    const std::size_t before = dev.textures.size();

    // The cut: Cybil is gone, Harry's other material appears, and the device refuses the next uploads.
    dev.refuse_next = 3;
    bool alt_seen = false;
    for (int f = 0; f < 200 && !alt_seen; ++f) {
        add_mesh(hero_key, 1448, 1086, hero);
        add_mesh(hero_alt_key, 256, 192, hero_alt);
        SH_CHECK(scene.prepare(dev, sc::current_frame()));
        ShScene_FrameEnd();
        for (const auto& [id, t] : dev.textures)
            if (t.texels.size() == hero_alt.size() && std::memcmp(t.texels.data(), hero_alt.data(), hero_alt.size()) == 0)
                alt_seen = true;
    }
    SH_CHECK(alt_seen);
    SH_CHECK(dev.refuse_next == 0);
    SH_CHECK(dev.textures.size() == before + 1);
    SH_CHECK(dev.out_of_bounds_uploads == 0);
    // The ones made before the cut: same textures, uploaded once, never replaced or shrunk.
    SH_CHECK(same(dev.find(1448, 1086), hero));
    int cybil_like = 0;
    for (const auto& [id, t] : dev.textures)
        if (t.texels.size() == cybil.size() && std::memcmp(t.texels.data(), cybil.data(), cybil.size()) == 0) {
            ++cybil_like;
            SH_CHECK(t.uploads == 1);
        }
    SH_CHECK(cybil_like == 1);
    SH_CHECK(dev.find(724, 543) == nullptr);  // a refusal within the device limit is not answered by halving
    ShScene_FrameEnd();
}
