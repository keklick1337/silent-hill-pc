// Textures the splits sample besides video memory (RENDER.md section 3): the white texture of untextured
// prims, the replacement pictures (src/render/textures/replacement_store.h puts them here), RGBA
// textures of tools and mods. All are RHI textures addressed by a registry id, so no GL name leaves the backend.
//
// Uploads are recorded on a command list outside a render pass, and a texture can be created while the frame is
// being built (a lazily composed texture-pack page, a glTF material), so pixel data is queued here and sent by
// flush(), which the renderer calls on its frame list before the first pass of a flush.
#pragma once

#include "render/gpu/gpu_types.h"
#include "render/rhi/rhi.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sh::gpu {

class Textures {
public:
    explicit Textures(rhi::Device& device);
    ~Textures();
    Textures(const Textures&) = delete;
    Textures& operator=(const Textures&) = delete;

    // RGBA8 texture, rows top first; returns a registry id (never 0) or 0 on failure. Any size, non-power-of-two
    // included; a texture the device refuses (or above set_max_size) is box-halved until it fits, so size()
    // reports what was really created.
    std::uint32_t create_rgba(int width, int height, std::span<const std::uint8_t> rgba, const char* name);
    // `rect` is in the texels of the size passed to create_rgba; a halved texture gets the rect halved with it.
    void update_rgba(std::uint32_t id, const rhi::Rect& rect, std::span<const std::uint8_t> rgba);
    void destroy(std::uint32_t id);
    void set_nearest(std::uint32_t id, bool nearest);  // point sampling instead of the preset filter
    bool nearest(std::uint32_t id) const;

    rhi::TextureH handle(std::uint32_t id) const;
    rhi::SamplerH sampler(std::uint32_t id) const;
    bool size(std::uint32_t id, int* w, int* h) const;
    rhi::TextureH white() const;
    rhi::SamplerH nearest_sampler() const { return nearest_sampler_; }

    // Resolves what a split samples (Kind::Vram is resolved by the caller from Vram::texture). Empty for an id
    // that is not live.
    rhi::TextureH resolve(const TextureRef& ref) const;

    // The preset filter of non-nearest textures, the `texture_filter` key: 0 point, 1+ linear (the RHI has no
    // mip chains or anisotropy, so trilinear and anisotropic sample linear).
    void set_filter(int texture_filter) { filter_ = texture_filter; }
    int filter() const { return filter_; }
    // Device limit in texels when known (0 = unknown: only a createTexture failure halves).
    void set_max_size(int texels) { max_size_ = texels; }

    // Sends the queued uploads (outside a render pass on `cmd`).
    void flush(rhi::CommandList& cmd);
    std::size_t pending_uploads() const { return pending_.size(); }
    std::size_t live_count() const;

private:
    struct Entry {
        rhi::TextureH texture;
        int width = 0, height = 0;  // created size
        int shift = 0;              // halvings applied to the requested size
        bool nearest = false;
        bool live = false;
    };
    struct Upload {
        rhi::TextureH texture;
        rhi::Rect rect;
        std::vector<std::uint8_t> rgba;
    };

    const Entry* find(std::uint32_t id) const;
    void queue(rhi::TextureH texture, const rhi::Rect& rect, std::vector<std::uint8_t> rgba);

    rhi::Device& device_;
    std::vector<Entry> entries_;  // id = index + 1
    std::vector<std::uint32_t> free_;
    std::vector<Upload> pending_;
    std::uint32_t white_id_ = 0;
    rhi::SamplerH nearest_sampler_;
    rhi::SamplerH linear_sampler_;
    int filter_ = 0;
    int max_size_ = 0;
};

}  // namespace sh::gpu
