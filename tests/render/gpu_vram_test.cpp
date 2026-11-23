// sh::gpu::Vram: the CPU words of the PS1 video memory and their RG8 texture.

#include "framework/sh_test.h"
#include "render/gpu/vram.h"

#include <cstring>
#include <numeric>
#include <vector>

namespace {

using sh::Result;
using sh::gpu::Rect;
using sh::gpu::Vram;
namespace rhi = sh::rhi;

// Records texture creation and uploads; everything else is a no-op.
class RecordingDevice final : public rhi::Device, public rhi::CommandList {
public:
    struct Upload {
        rhi::TextureH texture;
        rhi::Rect rect;
        std::vector<std::byte> bytes;
    };
    rhi::Caps caps_info;
    rhi::TextureDesc created{};
    int destroyed = 0;
    std::uint32_t next = 1;
    std::vector<Upload> uploads;

    rhi::Backend backend() const override { return rhi::Backend::Gl33; }
    const rhi::Caps& caps() const override { return caps_info; }
    Result<rhi::TextureH> createTexture(const rhi::TextureDesc& desc) override {
        created = desc;
        return Result<rhi::TextureH>::ok(rhi::TextureH{next++});
    }
    Result<rhi::BufferH> createBuffer(const rhi::BufferDesc&) override { return Result<rhi::BufferH>::ok({next++}); }
    Result<rhi::SamplerH> createSampler(const rhi::SamplerDesc&) override {
        return Result<rhi::SamplerH>::ok({next++});
    }
    Result<rhi::ShaderH> createShader(const rhi::ShaderDesc&) override { return Result<rhi::ShaderH>::ok({next++}); }
    Result<rhi::PipelineH> createPipeline(const rhi::PipelineDesc&) override {
        return Result<rhi::PipelineH>::ok({next++});
    }
    void destroy(rhi::TextureH) override { ++destroyed; }
    void destroy(rhi::BufferH) override {}
    void destroy(rhi::SamplerH) override {}
    void destroy(rhi::ShaderH) override {}
    void destroy(rhi::PipelineH) override {}
    rhi::CommandList* begin() override { return this; }
    Result<void> submit() override { return Result<void>::ok(); }
    Result<void> readback(rhi::TextureH, const rhi::Rect&, std::span<std::byte>) override {
        return Result<void>::ok();
    }
    void waitIdle() override {}

    void uploadTexture(rhi::TextureH texture, const rhi::Rect& rect, std::span<const std::byte> pixels) override {
        uploads.push_back({texture, rect, std::vector<std::byte>(pixels.begin(), pixels.end())});
    }
    void uploadBuffer(rhi::BufferH, std::uint32_t, std::span<const std::byte>) override {}
    void copyTexture(rhi::TextureH, const rhi::Rect&, rhi::TextureH, std::int32_t, std::int32_t) override {}
    void blitTexture(rhi::TextureH, const rhi::Rect&, rhi::TextureH, const rhi::Rect&, rhi::Filter) override {}
    void resolveTexture(rhi::TextureH, rhi::TextureH) override {}
    rhi::RenderPass* beginRenderPass(const rhi::RenderPassDesc&) override { return nullptr; }
    void endRenderPass() override {}
    rhi::TextureH acquireSwapchainTexture(std::uint32_t*, std::uint32_t*) override { return {}; }
};

std::uint16_t at(const Vram& v, int x, int y) { return v.words()[static_cast<std::size_t>(y) * Vram::kWidth + x]; }

Rect rect(int x, int y, int w, int h) {
    return Rect{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(w),
                static_cast<std::int16_t>(h)};
}

}  // namespace

SH_TEST(gpu_vram_load_store_wrap) {
    Vram v;
    std::vector<std::uint16_t> src(4 * 3);
    std::iota(src.begin(), src.end(), std::uint16_t{1});
    v.load(rect(1022, 510, 4, 3), src.data());
    SH_CHECK(at(v, 1022, 510) == 1);
    SH_CHECK(at(v, 1023, 510) == 2);
    SH_CHECK(at(v, 0, 510) == 3);
    SH_CHECK(at(v, 1, 510) == 4);
    SH_CHECK(at(v, 1022, 511) == 5);
    SH_CHECK(at(v, 0, 0) == 11);  // third row wrapped to y 0, third column to x 0
    SH_CHECK(at(v, 1, 0) == 12);

    std::vector<std::uint16_t> back(src.size(), 0);
    v.store(rect(1022, 510, 4, 3), back.data());
    SH_CHECK(back == src);
    // Negative origins wrap too.
    std::uint16_t one = 0;
    v.store(rect(-2, -2, 1, 1), &one);
    SH_CHECK(one == 1);
}

SH_TEST(gpu_vram_move_overlap_reads_source_first) {
    Vram v;
    std::vector<std::uint16_t> row(8);
    std::iota(row.begin(), row.end(), std::uint16_t{100});
    v.load(rect(10, 5, 8, 1), row.data());
    v.move(rect(10, 5, 8, 1), 12, 5);  // rightward overlap: a forward copy would smear
    for (int i = 0; i < 8; ++i) SH_CHECK(at(v, 12 + i, 5) == 100 + i);
    SH_CHECK(at(v, 10, 5) == 100);
    SH_CHECK(at(v, 11, 5) == 101);

    std::vector<std::uint16_t> col(4);
    std::iota(col.begin(), col.end(), std::uint16_t{7});
    v.load(rect(50, 100, 1, 4), col.data());
    v.move(rect(50, 100, 1, 4), 50, 101);  // downward overlap
    for (int i = 0; i < 4; ++i) SH_CHECK(at(v, 50, 101 + i) == 7 + i);

    // A move past the edge wraps the destination.
    v.move(rect(10, 5, 2, 1), 1023, 511);
    SH_CHECK(at(v, 1023, 511) == 100);
    SH_CHECK(at(v, 0, 511) == 101);
}

SH_TEST(gpu_vram_fill_packs_555_with_mask_bit_clear) {
    SH_CHECK(Vram::fill_word(0, 0, 0) == 0);
    SH_CHECK(Vram::fill_word(255, 255, 255) == 0x7FFF);
    SH_CHECK(Vram::fill_word(8, 0, 0) == 1);
    SH_CHECK(Vram::fill_word(0, 8, 0) == (1 << 5));
    SH_CHECK(Vram::fill_word(0, 0, 8) == (1 << 10));
    SH_CHECK(Vram::fill_word(7, 7, 7) == 0);

    Vram v;
    std::vector<std::uint16_t> stp(16, 0x8000);
    v.load(rect(1020, 0, 8, 2), stp.data());
    int heard = 0;
    v.on_change([&](const Rect&) { ++heard; });
    v.fill(rect(1020, 0, 8, 2), 248, 0, 128);
    SH_CHECK(heard == 0);
    const std::uint16_t want = 31 | (16 << 10);
    SH_CHECK(at(v, 1020, 0) == want);
    SH_CHECK(at(v, 3, 1) == want);
    SH_CHECK(at(v, 4, 0) == 0);
    SH_CHECK(at(v, 1020, 2) == 0);
}

SH_TEST(gpu_vram_dirty_union_and_listener) {
    Vram v;
    SH_CHECK(!v.dirty());
    std::vector<Rect> heard;
    v.on_change([&](const Rect& r) { heard.push_back(r); });
    std::uint16_t word = 1;
    v.load(rect(10, 20, 1, 1), &word);
    v.mark_dirty(rect(100, 5, 10, 4));
    SH_CHECK(v.dirty());
    SH_CHECK(v.dirty_rect() == rect(10, 5, 100, 16));
    SH_CHECK(heard.size() == 2);
    SH_CHECK(heard[0] == rect(10, 20, 1, 1));

    // A wrapping rect is told as its in-range pieces.
    heard.clear();
    v.mark_dirty(rect(1020, 510, 8, 4));
    SH_CHECK(heard.size() == 4);
    SH_CHECK(heard[0] == rect(1020, 510, 4, 2));
    SH_CHECK(heard[1] == rect(0, 510, 4, 2));
    SH_CHECK(heard[2] == rect(1020, 0, 4, 2));
    SH_CHECK(heard[3] == rect(0, 0, 4, 2));
    SH_CHECK(v.dirty_rect() == rect(0, 0, 1024, 512));

    v.mark_dirty(rect(0, 0, 0, 5));  // empty: nothing
    SH_CHECK(heard.size() == 4);
}

SH_TEST(gpu_vram_upload_sends_dirty_rect_as_rg8) {
    RecordingDevice dev;
    Vram v;
    SH_CHECK(static_cast<bool>(v.create_texture(dev)));
    SH_CHECK(dev.created.format == rhi::TextureFormat::RG8);
    SH_CHECK(dev.created.width == 1024 && dev.created.height == 512);
    SH_CHECK((dev.created.usage & rhi::kUsageSampled) != 0);

    // First upload sends the whole memory.
    v.upload(dev);
    SH_CHECK(dev.uploads.size() == 1);
    SH_CHECK(dev.uploads[0].rect.x == 0 && dev.uploads[0].rect.w == 1024 && dev.uploads[0].rect.h == 512);
    SH_CHECK(dev.uploads[0].bytes.size() == 1024u * 512u * 2u);
    SH_CHECK(!v.dirty());
    v.upload(dev);  // nothing dirty: nothing sent
    SH_CHECK(dev.uploads.size() == 1);

    std::vector<Rect> sent;
    v.on_upload([&](const Rect& r) { sent.push_back(r); });
    const std::uint16_t words[4] = {0x1234, 0xABCD, 0x8001, 0x00FF};
    v.load(rect(300, 40, 2, 2), words);
    v.upload(dev);
    SH_CHECK(dev.uploads.size() == 2);
    const auto& up = dev.uploads[1];
    SH_CHECK(up.texture == v.texture());
    SH_CHECK(up.rect.x == 300 && up.rect.y == 40 && up.rect.w == 2 && up.rect.h == 2);
    const std::uint8_t want[8] = {0x34, 0x12, 0xCD, 0xAB, 0x01, 0x80, 0xFF, 0x00};
    SH_CHECK(up.bytes.size() == 8);
    SH_CHECK(std::memcmp(up.bytes.data(), want, 8) == 0);
    SH_CHECK(sent.size() == 1 && sent[0] == rect(300, 40, 2, 2));

    v.release(dev);
    SH_CHECK(dev.destroyed == 1);
    SH_CHECK(!v.texture());
    v.mark_dirty(rect(0, 0, 4, 4));
    v.upload(dev);  // no texture: kept dirty
    SH_CHECK(v.dirty());
    SH_CHECK(dev.uploads.size() == 2);
}
