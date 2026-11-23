#include "render/gpu/vram.h"

#include <algorithm>
#include <cstring>

namespace sh::gpu {

namespace {

constexpr int kMaskX = Vram::kWidth - 1;
constexpr int kMaskY = Vram::kHeight - 1;

std::size_t index(int x, int y) { return static_cast<std::size_t>(y & kMaskY) * Vram::kWidth + (x & kMaskX); }

Rect make_rect(int x, int y, int w, int h) {
    return Rect{static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), static_cast<std::int16_t>(w),
                static_cast<std::int16_t>(h)};
}

// Calls f(x, y, w, h) for the up to four in-range pieces a wrapping rect covers. A side longer than the memory
// covers it once.
template <class F>
void for_each_piece(const Rect& rect, F&& f) {
    if (rect.w <= 0 || rect.h <= 0) return;
    const int w = std::min<int>(rect.w, Vram::kWidth);
    const int h = std::min<int>(rect.h, Vram::kHeight);
    const int x0 = rect.x & kMaskX, y0 = rect.y & kMaskY;
    const int wa = std::min(w, Vram::kWidth - x0), ha = std::min(h, Vram::kHeight - y0);
    f(x0, y0, wa, ha);
    if (wa < w) f(0, y0, w - wa, ha);
    if (ha < h) {
        f(x0, 0, wa, h - ha);
        if (wa < w) f(0, 0, w - wa, h - ha);
    }
}

}  // namespace

Vram::Vram() : words_(static_cast<std::size_t>(kWidth) * kHeight, 0) {}

Result<void> Vram::create_texture(rhi::Device& device) {
    release(device);
    auto tex = device.createTexture(
        {rhi::TextureFormat::RG8, static_cast<std::uint32_t>(kWidth), static_cast<std::uint32_t>(kHeight),
         rhi::kUsageSampled, "gpu_vram"});
    if (!tex) return Result<void>::fail(tex.error());
    texture_ = tex.value();
    // A fresh texture holds nothing: the whole memory goes with the next upload.
    add_dirty(make_rect(0, 0, kWidth, kHeight));
    return Result<void>::ok();
}

void Vram::release(rhi::Device& device) {
    if (texture_) device.destroy(texture_);
    texture_ = {};
}

void Vram::load(const Rect& rect, const std::uint16_t* src) {
    if (rect.w <= 0 || rect.h <= 0 || src == nullptr) return;
    // Rows and columns are written in order, so a side longer than the memory overwrites itself like the GPU.
    for (int row = 0; row < rect.h; ++row) {
        std::uint16_t* line = &words_[index(0, rect.y + row)];
        const std::uint16_t* in = src + static_cast<std::size_t>(row) * rect.w;
        int x = rect.x & kMaskX;
        for (int left = rect.w; left > 0;) {
            const int n = std::min(left, kWidth - x);
            std::memcpy(line + x, in, static_cast<std::size_t>(n) * sizeof(std::uint16_t));
            in += n;
            left -= n;
            x = 0;
        }
    }
    touch(rect, true);
}

void Vram::store(const Rect& rect, std::uint16_t* dst) const {
    if (rect.w <= 0 || rect.h <= 0 || dst == nullptr) return;
    for (int row = 0; row < rect.h; ++row) {
        const std::uint16_t* line = &words_[index(0, rect.y + row)];
        std::uint16_t* out = dst + static_cast<std::size_t>(row) * rect.w;
        int x = rect.x & kMaskX;
        for (int left = rect.w; left > 0;) {
            const int n = std::min(left, kWidth - x);
            std::memcpy(out, line + x, static_cast<std::size_t>(n) * sizeof(std::uint16_t));
            out += n;
            left -= n;
            x = 0;
        }
    }
}

void Vram::move(const Rect& src, int dst_x, int dst_y) {
    if (src.w <= 0 || src.h <= 0) return;
    std::vector<std::uint16_t> block(static_cast<std::size_t>(src.w) * src.h);
    store(src, block.data());
    load(make_rect(dst_x, dst_y, src.w, src.h), block.data());
}

void Vram::fill(const Rect& rect, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    const std::uint16_t word = fill_word(r, g, b);
    for_each_piece(rect, [&](int x, int y, int w, int h) {
        for (int row = 0; row < h; ++row) std::fill_n(&words_[index(x, y + row)], w, word);
    });
    touch(rect, false);
}

void Vram::mark_dirty(const Rect& rect) { touch(rect, true); }

void Vram::touch(const Rect& rect, bool notify) {
    for_each_piece(rect, [&](int x, int y, int w, int h) {
        const Rect piece = make_rect(x, y, w, h);
        add_dirty(piece);
        if (notify && listener_) listener_(piece);
    });
}

void Vram::add_dirty(const Rect& r) {
    if (dirty_.w <= 0 || dirty_.h <= 0) {
        dirty_ = r;
        return;
    }
    const int x0 = std::min(dirty_.x, r.x), y0 = std::min(dirty_.y, r.y);
    const int x1 = std::max(dirty_.x + dirty_.w, r.x + r.w), y1 = std::max(dirty_.y + dirty_.h, r.y + r.h);
    dirty_ = make_rect(x0, y0, x1 - x0, y1 - y0);
}

void Vram::upload(rhi::CommandList& cmd) {
    if (!dirty() || !texture_) return;
    const Rect d = dirty_;
    // RG8 texel = low byte in R, high byte in G (prim_batch.frag VRAM()); packed explicitly, not by host byte order.
    staging_.resize(static_cast<std::size_t>(d.w) * d.h * 2);
    std::byte* out = staging_.data();
    for (int y = 0; y < d.h; ++y) {
        const std::uint16_t* line = &words_[index(d.x, d.y + y)];
        for (int x = 0; x < d.w; ++x) {
            *out++ = static_cast<std::byte>(line[x] & 0xFF);
            *out++ = static_cast<std::byte>(line[x] >> 8);
        }
    }
    cmd.uploadTexture(texture_, rhi::Rect{d.x, d.y, d.w, d.h}, staging_);
    dirty_ = {};
    if (upload_listener_) upload_listener_(d);
}

}  // namespace sh::gpu
