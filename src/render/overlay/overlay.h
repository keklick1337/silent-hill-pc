// The engine's 2D overlay: the PC UI and debug drawers (console, debug panels, PC dialogs,
// mouse pointer) queue textured and coloured quads and lines here during the frame, and the present step records
// them on the swapchain through the RHI after the picture, so they draw the same under GL and Vulkan.
//
// Coordinates are pixels with the origin at the top-left, +Y down, in one of two spaces: the whole target
// (Space::Target) or the picture rectangle inside it (Space::Picture: the letterboxed game image the drawers lay
// out against). Colours are straight (not premultiplied) RGBA; everything blends source-over.
#pragma once

#include "formats/common/status.h"
#include "render/rhi/rhi.h"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace sh::overlay {

struct Color {
    float r = 1.f, g = 1.f, b = 1.f, a = 1.f;
    bool operator==(const Color&) const = default;
};

struct Rect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    bool operator==(const Rect&) const = default;
};

// Integer pixels of the target, top-left origin.
struct PixelRect {
    std::int32_t x = 0, y = 0, w = 0, h = 0;
    bool operator==(const PixelRect&) const = default;
};

enum class Space : std::uint8_t { Target, Picture };

enum class Filter : std::uint8_t { Nearest, Linear };

// Rgba8: four bytes per texel. Alpha8: one coverage byte per texel, sampled as white with that alpha (font atlases).
enum class Format : std::uint8_t { Rgba8, Alpha8 };

// 0 is "no texture": the quad is the flat colour.
using TextureId = std::uint32_t;

// The frame the drawers lay out against. The target is the whole surface the overlay lands on; the picture is
// where the game image sits inside it.
struct Frame {
    std::int32_t target_w = 0, target_h = 0;
    Rect picture;
    bool valid() const { return target_w > 0 && target_h > 0 && picture.w > 0.f && picture.h > 0.f; }
};

// One vertex in target pixels. rgba is the colour as bytes r, g, b, a.
struct Vertex {
    float x = 0.f, y = 0.f, u = 0.f, v = 0.f;
    std::uint8_t rgba[4] = {255, 255, 255, 255};
};
static_assert(sizeof(Vertex) == 20);

// A run of triangles drawn with one texture and one scissor.
struct Batch {
    TextureId texture = 0;
    bool scissored = false;
    PixelRect scissor;
    std::uint32_t first = 0;  // first vertex
    std::uint32_t count = 0;  // vertices, a multiple of 3
};

// CPU copies of the overlay's textures. Ids are never reused, so a GPU mirror can tell a replaced texture by its
// version and a destroyed one by its absence.
class TextureStore {
public:
    struct Texture {
        std::int32_t w = 0, h = 0;
        Format format = Format::Rgba8;
        Filter filter = Filter::Nearest;
        std::vector<std::uint8_t> pixels;  // tightly packed, top row first
        std::uint64_t version = 0;
    };

    // `pixels` may be null (the texture starts transparent). Returns 0 for an empty or oversized texture.
    TextureId create(std::int32_t w, std::int32_t h, Format format, Filter filter, const std::uint8_t* pixels);
    // Replaces the contents; a different size reallocates. Unknown ids are ignored.
    void update(TextureId id, std::int32_t w, std::int32_t h, const std::uint8_t* pixels);
    // Removed at the next collect(), so a quad queued this frame still finds it.
    void destroy(TextureId id);
    void collect();

    const Texture* find(TextureId id) const;
    std::size_t size() const { return textures_.size(); }

private:
    std::unordered_map<TextureId, Texture> textures_;
    std::vector<TextureId> doomed_;
    TextureId next_ = 1;
    std::uint64_t version_ = 0;
};

// The frame's geometry: vertices in target pixels and the batches over them, in submission order.
class DrawList {
public:
    void reset(const Frame& frame);
    const Frame& frame() const { return frame_; }

    // Clips everything queued after it (in `space`) until clear_scissor. Empty rectangles clip everything.
    void set_scissor(Space space, const Rect& rect);
    void clear_scissor();

    // An axis-aligned quad; uv spans the texture (0..1).
    void quad(Space space, const Rect& rect, const Color& color, TextureId texture = 0,
              const Rect& uv = {0.f, 0.f, 1.f, 1.f});
    void rect(Space space, const Rect& rect, const Color& color) { quad(space, rect, color, 0); }
    // A segment drawn as a quad `width` pixels across (no line primitive: Vulkan has no wide lines).
    void line(Space space, float x0, float y0, float x1, float y1, float width, const Color& color);
    // Free triangles (vertices in `space`, count a multiple of 3; a remainder is dropped).
    void triangles(Space space, std::span<const Vertex> vertices, TextureId texture);

    bool empty() const { return batches_.empty(); }
    const std::vector<Vertex>& vertices() const { return vertices_; }
    const std::vector<Batch>& batches() const { return batches_; }

    // Space -> target pixels.
    float to_target_x(Space space, float x) const { return space == Space::Picture ? x + frame_.picture.x : x; }
    float to_target_y(Space space, float y) const { return space == Space::Picture ? y + frame_.picture.y : y; }

private:
    void append(TextureId texture, std::span<const Vertex> vertices);

    Frame frame_;
    bool scissored_ = false;
    PixelRect scissor_;
    std::vector<Vertex> vertices_;
    std::vector<Batch> batches_;
};

class GpuRenderer;

// The process-wide overlay the drawers queue into.
class Overlay {
public:
    Overlay();
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    // Starts a frame: drops last frame's geometry and the textures destroyed since.
    void begin_frame(const Frame& frame);
    const Frame& frame() const { return list_.frame(); }
    // The picture size, for drawers that lay out in Space::Picture.
    float width() const { return list_.frame().picture.w; }
    float height() const { return list_.frame().picture.h; }

    TextureStore& textures() { return textures_; }
    DrawList& list() { return list_; }
    const DrawList& list() const { return list_; }

    // Shorthands for list().
    void quad(Space space, const Rect& rect, const Color& color, TextureId texture = 0,
              const Rect& uv = {0.f, 0.f, 1.f, 1.f}) {
        list_.quad(space, rect, color, texture, uv);
    }
    void rect(Space space, const Rect& rect, const Color& color) { list_.rect(space, rect, color); }
    void line(Space space, float x0, float y0, float x1, float y1, float width, const Color& color) {
        list_.line(space, x0, y0, x1, y1, width, color);
    }

    // Records the frame onto `target` (w x h, the frame's target size) after the picture: uploads, then one
    // render pass that loads and keeps the target. Outside a render pass. The GPU objects live on `device` until
    // release_gpu() or the next record on another device.
    Result<void> record(rhi::Device& device, rhi::CommandList& cmd, rhi::TextureH target,
                        std::uint32_t w, std::uint32_t h);

    // For a host that opens the pass itself (the GL executor, whose framebuffer the RHI cannot
    // name): prepare() uploads outside a pass, draw() records into the open pass. The pass must already cover
    // the target with a w x h viewport; `scissor` applies a batch's clip (null = none) the way that pass can.
    Result<void> prepare(rhi::Device& device, rhi::CommandList& cmd);
    using ScissorFn = void (*)(void* user, const PixelRect* rect);
    void draw(rhi::RenderPass& pass, std::uint32_t w, std::uint32_t h, ScissorFn scissor, void* user);

    // Frees the GPU objects (before their device goes away).
    void release_gpu();

private:
    TextureStore textures_;
    DrawList list_;
    std::unique_ptr<GpuRenderer> gpu_;
    rhi::Device* gpuDevice_ = nullptr;
};

Overlay& overlay();

// Packs a colour into vertex bytes (clamped, rounded).
void pack(const Color& color, std::uint8_t out[4]);

}  // namespace sh::overlay
