// Video memory of the PS1 GPU as the engine keeps it (RENDER.md section 3): 1024x512 16-bit words on the
// CPU (the game's LoadImage/StoreImage/MoveImage/ClearImage work on them) and the RHI texture the shaders sample,
// updated from the dirty rect once per flush.
#pragma once

#include "render/gpu/gpu_types.h"
#include "render/rhi/rhi.h"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace sh::gpu {

class Vram {
public:
    static constexpr int kWidth = 1024;
    static constexpr int kHeight = 512;

    Vram();
    // Creates the texture on `device` (RG8 1024x512 as prim_batch.frag samples it). Safe to call again after a reset.
    Result<void> create_texture(rhi::Device& device);
    void release(rhi::Device& device);

    std::span<std::uint16_t> words() { return words_; }
    std::span<const std::uint16_t> words() const { return words_; }

    // Transfers wrap the way the PS1 GPU wraps them (x mod 1024, y mod 512). A rect with w or h <= 0 does nothing.
    // load/store consume/produce rect.w * rect.h tightly packed words, top row first.
    void load(const Rect& rect, const std::uint16_t* src);
    void store(const Rect& rect, std::uint16_t* dst) const;
    // Overlapping source and destination behave as if the source were read whole first (memmove).
    void move(const Rect& src, int dst_x, int dst_y);
    // ClearImage: the colour becomes the PS1 fill word (5 bits per channel, the mask/STP bit 15 cleared). A fill
    // is not a game upload, so the change listener is not told of it.
    void fill(const Rect& rect, std::uint8_t r, std::uint8_t g, std::uint8_t b);
    static constexpr std::uint16_t fill_word(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        return static_cast<std::uint16_t>((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
    }

    // Marks a rect changed: it is sent with the next upload and the listener hears of it (hi-res override cache
    // invalidation, feedback bookkeeping). A rect past the edges is split at the wrap.
    void mark_dirty(const Rect& rect);
    bool dirty() const { return dirty_.w > 0; }
    Rect dirty_rect() const { return dirty_; }
    // Sends the dirty rect to the texture (outside a render pass on `cmd`) and clears it. Without a texture the
    // rect stays dirty.
    void upload(rhi::CommandList& cmd);

    rhi::TextureH texture() const { return texture_; }

    using Listener = std::function<void(const Rect&)>;
    void on_change(Listener listener) { listener_ = std::move(listener); }
    // Hears every rect upload() sent: the feedback pass stamps the stored frame over the feedback pages again.
    void on_upload(Listener listener) { upload_listener_ = std::move(listener); }

private:
    void touch(const Rect& rect, bool notify);
    void add_dirty(const Rect& in_range);

    std::vector<std::uint16_t> words_;
    Rect dirty_{};
    rhi::TextureH texture_;
    Listener listener_;
    Listener upload_listener_;
    std::vector<std::byte> staging_;
};

}  // namespace sh::gpu
