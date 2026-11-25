#include "render/overlay/overlay.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sh::overlay {

namespace {

constexpr std::int32_t kMaxTextureSide = 8192;

std::size_t bytes_per_texel(Format f) { return f == Format::Alpha8 ? 1u : 4u; }

std::uint8_t to_byte(float v) {
    const float c = std::clamp(v, 0.f, 1.f);
    return static_cast<std::uint8_t>(std::lround(c * 255.f));
}

}  // namespace

void pack(const Color& color, std::uint8_t out[4]) {
    out[0] = to_byte(color.r);
    out[1] = to_byte(color.g);
    out[2] = to_byte(color.b);
    out[3] = to_byte(color.a);
}

// ---- textures ---------------------------------------------------------------------------------------------

TextureId TextureStore::create(std::int32_t w, std::int32_t h, Format format, Filter filter,
                               const std::uint8_t* pixels) {
    if (w <= 0 || h <= 0 || w > kMaxTextureSide || h > kMaxTextureSide) return 0;
    const TextureId id = next_++;
    Texture& t = textures_[id];
    t.w = w;
    t.h = h;
    t.format = format;
    t.filter = filter;
    t.pixels.assign(std::size_t(w) * std::size_t(h) * bytes_per_texel(format), 0);
    if (pixels) std::memcpy(t.pixels.data(), pixels, t.pixels.size());
    t.version = ++version_;
    return id;
}

void TextureStore::update(TextureId id, std::int32_t w, std::int32_t h, const std::uint8_t* pixels) {
    auto it = textures_.find(id);
    if (it == textures_.end() || w <= 0 || h <= 0 || w > kMaxTextureSide || h > kMaxTextureSide) return;
    Texture& t = it->second;
    t.w = w;
    t.h = h;
    t.pixels.resize(std::size_t(w) * std::size_t(h) * bytes_per_texel(t.format));
    if (pixels)
        std::memcpy(t.pixels.data(), pixels, t.pixels.size());
    else
        std::fill(t.pixels.begin(), t.pixels.end(), std::uint8_t{0});
    t.version = ++version_;
}

void TextureStore::destroy(TextureId id) {
    if (id && textures_.count(id)) doomed_.push_back(id);
}

void TextureStore::collect() {
    for (TextureId id : doomed_) textures_.erase(id);
    doomed_.clear();
}

const TextureStore::Texture* TextureStore::find(TextureId id) const {
    auto it = textures_.find(id);
    return it == textures_.end() ? nullptr : &it->second;
}

// ---- draw list --------------------------------------------------------------------------------------------

void DrawList::reset(const Frame& frame) {
    frame_ = frame;
    scissored_ = false;
    scissor_ = {};
    vertices_.clear();
    batches_.clear();
}

void DrawList::set_scissor(Space space, const Rect& rect) {
    // Outward to whole pixels, then onto the target: a clip never hides a pixel the rectangle touches.
    const float x0 = std::floor(to_target_x(space, rect.x)), y0 = std::floor(to_target_y(space, rect.y));
    const float x1 = std::ceil(to_target_x(space, rect.x + rect.w)), y1 = std::ceil(to_target_y(space, rect.y + rect.h));
    const auto cx0 = static_cast<std::int32_t>(std::clamp(x0, 0.f, float(frame_.target_w)));
    const auto cy0 = static_cast<std::int32_t>(std::clamp(y0, 0.f, float(frame_.target_h)));
    const auto cx1 = static_cast<std::int32_t>(std::clamp(x1, 0.f, float(frame_.target_w)));
    const auto cy1 = static_cast<std::int32_t>(std::clamp(y1, 0.f, float(frame_.target_h)));
    scissored_ = true;
    scissor_ = {cx0, cy0, std::max(0, cx1 - cx0), std::max(0, cy1 - cy0)};
}

void DrawList::clear_scissor() {
    scissored_ = false;
    scissor_ = {};
}

void DrawList::append(TextureId texture, std::span<const Vertex> vertices) {
    if (vertices.empty()) return;
    if (scissored_ && (scissor_.w == 0 || scissor_.h == 0)) return;  // clipped away
    const auto first = static_cast<std::uint32_t>(vertices_.size());
    vertices_.insert(vertices_.end(), vertices.begin(), vertices.end());
    if (!batches_.empty()) {
        Batch& last = batches_.back();
        if (last.texture == texture && last.scissored == scissored_ && (!scissored_ || last.scissor == scissor_) &&
            last.first + last.count == first) {
            last.count += static_cast<std::uint32_t>(vertices.size());
            return;
        }
    }
    batches_.push_back({texture, scissored_, scissored_ ? scissor_ : PixelRect{}, first,
                        static_cast<std::uint32_t>(vertices.size())});
}

void DrawList::quad(Space space, const Rect& r, const Color& color, TextureId texture, const Rect& uv) {
    if (r.w == 0.f || r.h == 0.f || color.a <= 0.f) return;
    const float x0 = to_target_x(space, r.x), y0 = to_target_y(space, r.y);
    const float x1 = to_target_x(space, r.x + r.w), y1 = to_target_y(space, r.y + r.h);
    const float u0 = uv.x, v0 = uv.y, u1 = uv.x + uv.w, v1 = uv.y + uv.h;
    Vertex q[6];
    q[0] = {x0, y0, u0, v0, {}};
    q[1] = {x1, y0, u1, v0, {}};
    q[2] = {x0, y1, u0, v1, {}};
    q[3] = q[2];
    q[4] = q[1];
    q[5] = {x1, y1, u1, v1, {}};
    std::uint8_t c[4];
    pack(color, c);
    for (Vertex& v : q) std::memcpy(v.rgba, c, 4);
    append(texture, q);
}

void DrawList::line(Space space, float x0, float y0, float x1, float y1, float width, const Color& color) {
    if (width <= 0.f || color.a <= 0.f) return;
    const float ax = to_target_x(space, x0), ay = to_target_y(space, y0);
    const float bx = to_target_x(space, x1), by = to_target_y(space, y1);
    const float dx = bx - ax, dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.f) return;
    // Half the width either side of the segment, across its direction.
    const float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    std::uint8_t c[4];
    pack(color, c);
    Vertex q[6];
    q[0] = {ax + nx, ay + ny, 0.f, 0.f, {}};
    q[1] = {bx + nx, by + ny, 0.f, 0.f, {}};
    q[2] = {ax - nx, ay - ny, 0.f, 0.f, {}};
    q[3] = q[2];
    q[4] = q[1];
    q[5] = {bx - nx, by - ny, 0.f, 0.f, {}};
    for (Vertex& v : q) std::memcpy(v.rgba, c, 4);
    append(0, q);
}

void DrawList::triangles(Space space, std::span<const Vertex> vertices, TextureId texture) {
    const std::size_t n = vertices.size() - vertices.size() % 3;
    if (n == 0) return;
    if (space == Space::Target) {
        append(texture, vertices.first(n));
        return;
    }
    std::vector<Vertex> moved(vertices.begin(), vertices.begin() + static_cast<std::ptrdiff_t>(n));
    for (Vertex& v : moved) {
        v.x = to_target_x(space, v.x);
        v.y = to_target_y(space, v.y);
    }
    append(texture, moved);
}

// ---- overlay ----------------------------------------------------------------------------------------------

Overlay& overlay() {
    // Never destroyed: the GL context goes away from atexit handlers, after function statics would be;
    // release_gpu() frees the GPU side while it is still current.
    static Overlay* o = new Overlay;
    return *o;
}

Overlay& window_overlay() {
    static Overlay* o = new Overlay;
    return *o;
}

void Overlay::begin_frame(const Frame& frame) {
    textures_.collect();
    list_.reset(frame);
}

}  // namespace sh::overlay
