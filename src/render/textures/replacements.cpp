// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "render/textures/replacements.h"

#include "engine/log/sh_log.h"
#include "formats/texture/picture.h"
#include "render/gpu/renderer.h"
#include "render/gpu/textures.h"

namespace sh::textures {

namespace {

// The renderer's registry, looked up on every call: the store outlives the renderer and exists before it.
class RendererTextures final : public TextureSink {
public:
    std::uint32_t create(int width, int height, std::span<const std::uint8_t> rgba) override {
        sh::gpu::Textures* t = textures();
        const std::uint32_t id = t != nullptr ? t->create_rgba(width, height, rgba, "replacement") : 0;
        if (id == 0 && t != nullptr)
            SH_WARN("[TEXTURES] a %dx%d replacement texture could not be created", width, height);
        return id;
    }
    void update(std::uint32_t id, int width, int height, std::span<const std::uint8_t> rgba) override {
        if (sh::gpu::Textures* t = textures()) t->update_rgba(id, sh::rhi::Rect{0, 0, width, height}, rgba);
    }
    void destroy(std::uint32_t id) override {
        if (sh::gpu::Textures* t = textures()) t->destroy(id);
    }
    void set_point_sampled(std::uint32_t id, bool on) override {
        if (sh::gpu::Textures* t = textures()) t->set_nearest(id, on);
    }

private:
    static sh::gpu::Textures* textures() {
        sh::gpu::Renderer* r = sh::gpu::renderer();
        return r != nullptr ? &r->textures() : nullptr;
    }
};

}  // namespace

ReplacementStore& replacements() {
    static RendererTextures sink;
    static ReplacementStore store(sink);
    return store;
}

std::optional<sh::formats::texture::RgbaImage> read_picture(const std::string& name,
                                                            std::span<const std::uint8_t> file) {
    Result<sh::formats::texture::RgbaImage> image = sh::formats::texture::decode_picture(file);
    if (!image) {
        SH_WARN("[TEXTURES] %s is not shown: %s", name.c_str(), image.error().c_str());
        return std::nullopt;
    }
    return std::move(image.value());
}

}  // namespace sh::textures
