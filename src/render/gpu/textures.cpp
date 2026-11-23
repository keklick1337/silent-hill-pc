#include "render/gpu/textures.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace sh::gpu {

namespace {

// One 2:1 box step; an odd last row or column folds into its neighbour rather than being sampled past the edge.
std::vector<std::uint8_t> halve(std::span<const std::uint8_t> src, int sw, int sh, int* dw, int* dh) {
    const int w = std::max(1, sw / 2), h = std::max(1, sh / 2);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const int y0 = std::min(y * 2, sh - 1), y1 = std::min(y * 2 + 1, sh - 1);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::min(x * 2, sw - 1), x1 = std::min(x * 2 + 1, sw - 1);
            const std::uint8_t* a = &src[(static_cast<std::size_t>(y0) * sw + x0) * 4];
            const std::uint8_t* b = &src[(static_cast<std::size_t>(y0) * sw + x1) * 4];
            const std::uint8_t* c = &src[(static_cast<std::size_t>(y1) * sw + x0) * 4];
            const std::uint8_t* d = &src[(static_cast<std::size_t>(y1) * sw + x1) * 4];
            std::uint8_t* o = &out[(static_cast<std::size_t>(y) * w + x) * 4];
            for (int k = 0; k < 4; ++k) o[k] = static_cast<std::uint8_t>((a[k] + b[k] + c[k] + d[k]) >> 2);
        }
    }
    *dw = w;
    *dh = h;
    return out;
}

std::vector<std::uint8_t> shrink(std::span<const std::uint8_t> src, int w, int h, int shift, int* ow, int* oh) {
    std::vector<std::uint8_t> cur(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(w) * h * 4);
    for (int i = 0; i < shift; ++i) {
        int nw = 0, nh = 0;
        cur = halve(cur, w, h, &nw, &nh);
        w = nw;
        h = nh;
    }
    *ow = w;
    *oh = h;
    return cur;
}

bool enough(std::span<const std::uint8_t> rgba, int w, int h) {
    return w > 0 && h > 0 && rgba.size() >= static_cast<std::size_t>(w) * h * 4;
}

}  // namespace

Textures::Textures(rhi::Device& device) : device_(device) {
    rhi::SamplerDesc point;
    if (auto s = device_.createSampler(point)) nearest_sampler_ = s.value();
    rhi::SamplerDesc linear;
    linear.minFilter = rhi::Filter::Linear;
    linear.magFilter = rhi::Filter::Linear;
    if (auto s = device_.createSampler(linear)) linear_sampler_ = s.value();

    // Untextured prims sample this; it is never filtered, so its single texel reads exactly white.
    static constexpr std::uint8_t kWhite[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    white_id_ = create_rgba(1, 1, kWhite, "gpu.white");
    if (white_id_ != 0) set_nearest(white_id_, true);
}

Textures::~Textures() {
    for (Entry& e : entries_)
        if (e.live) device_.destroy(e.texture);
    if (nearest_sampler_) device_.destroy(nearest_sampler_);
    if (linear_sampler_) device_.destroy(linear_sampler_);
}

const Textures::Entry* Textures::find(std::uint32_t id) const {
    if (id == 0 || id > entries_.size()) return nullptr;
    const Entry& e = entries_[id - 1];
    return e.live ? &e : nullptr;
}

void Textures::queue(rhi::TextureH texture, const rhi::Rect& rect, std::vector<std::uint8_t> rgba) {
    pending_.push_back(Upload{texture, rect, std::move(rgba)});
}

std::uint32_t Textures::create_rgba(int width, int height, std::span<const std::uint8_t> rgba, const char* name) {
    if (width <= 0 || height <= 0) return 0;
    if (!rgba.empty() && !enough(rgba, width, height)) return 0;

    int shift = 0;
    if (max_size_ > 0)
        while (std::max(width >> shift, height >> shift) > max_size_) ++shift;

    rhi::TextureDesc desc;
    desc.format = rhi::TextureFormat::RGBA8;
    desc.usage = rhi::kUsageSampled;
    desc.name = name ? std::string_view(name) : std::string_view("gpu.rgba");
    rhi::TextureH texture;
    int w = 0, h = 0;
    for (;; ++shift) {
        w = std::max(1, width >> shift);
        h = std::max(1, height >> shift);
        desc.width = static_cast<std::uint32_t>(w);
        desc.height = static_cast<std::uint32_t>(h);
        if (auto t = device_.createTexture(desc)) {
            texture = t.value();
            break;
        }
        // A refused allocation keeps the slot backed at a lower resolution: a missing override texture would
        // drop its prims outright (unbacked virtual clut), softer art is the graceful degrade.
        if (w == 1 && h == 1) return 0;
    }

    if (!rgba.empty()) {
        int sw = 0, sh = 0;
        std::vector<std::uint8_t> pixels = shrink(rgba, width, height, shift, &sw, &sh);
        queue(texture, rhi::Rect{0, 0, sw, sh}, std::move(pixels));
    } else {
        // Storage starts transparent rather than undefined, so a texture sampled before its first update shows
        // nothing instead of driver garbage.
        queue(texture, rhi::Rect{0, 0, w, h}, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4, 0));
    }

    std::uint32_t id;
    if (!free_.empty()) {
        id = free_.back();
        free_.pop_back();
    } else {
        entries_.emplace_back();
        id = static_cast<std::uint32_t>(entries_.size());
    }
    Entry& e = entries_[id - 1];
    e = Entry{};
    e.texture = texture;
    e.width = w;
    e.height = h;
    e.shift = shift;
    e.live = true;
    return id;
}

void Textures::update_rgba(std::uint32_t id, const rhi::Rect& rect, std::span<const std::uint8_t> rgba) {
    const Entry* e = find(id);
    if (!e || !enough(rgba, rect.w, rect.h) || rect.x < 0 || rect.y < 0) return;
    int sw = rect.w, sh = rect.h;
    std::vector<std::uint8_t> pixels = shrink(rgba, rect.w, rect.h, e->shift, &sw, &sh);
    rhi::Rect dst{rect.x >> e->shift, rect.y >> e->shift, sw, sh};
    if (dst.x >= e->width || dst.y >= e->height) return;
    const int cw = std::min(dst.w, e->width - dst.x), ch = std::min(dst.h, e->height - dst.y);
    if (cw != dst.w || ch != dst.h) {
        std::vector<std::uint8_t> clipped(static_cast<std::size_t>(cw) * ch * 4);
        for (int y = 0; y < ch; ++y)
            std::memcpy(&clipped[static_cast<std::size_t>(y) * cw * 4], &pixels[static_cast<std::size_t>(y) * dst.w * 4],
                        static_cast<std::size_t>(cw) * 4);
        pixels = std::move(clipped);
        dst.w = cw;
        dst.h = ch;
    }
    queue(e->texture, dst, std::move(pixels));
}

void Textures::destroy(std::uint32_t id) {
    if (!find(id) || id == white_id_) return;
    Entry& e = entries_[id - 1];
    std::erase_if(pending_, [&](const Upload& u) { return u.texture == e.texture; });
    device_.destroy(e.texture);
    e = Entry{};
    free_.push_back(id);
}

void Textures::set_nearest(std::uint32_t id, bool nearest) {
    if (!find(id)) return;
    entries_[id - 1].nearest = nearest;
}

bool Textures::nearest(std::uint32_t id) const {
    const Entry* e = find(id);
    return e && e->nearest;
}

rhi::TextureH Textures::handle(std::uint32_t id) const {
    const Entry* e = find(id);
    return e ? e->texture : rhi::TextureH{};
}

rhi::SamplerH Textures::sampler(std::uint32_t id) const {
    // A loader's point-sampling choice (gutterless font atlases, native-resolution decodes) outranks the preset,
    // which is read at bind time so a runtime change of the setting reaches textures that already exist.
    const Entry* e = find(id);
    if (!e || e->nearest || filter_ <= 0 || !linear_sampler_) return nearest_sampler_;
    return linear_sampler_;
}

bool Textures::size(std::uint32_t id, int* w, int* h) const {
    const Entry* e = find(id);
    if (!e) return false;
    if (w) *w = e->width;
    if (h) *h = e->height;
    return true;
}

rhi::TextureH Textures::white() const { return handle(white_id_); }

rhi::TextureH Textures::resolve(const TextureRef& ref) const {
    switch (ref.kind) {
    case TextureRef::Kind::White:
        return white();
    case TextureRef::Kind::Vram:
        return {};
    case TextureRef::Kind::Override:
    case TextureRef::Kind::Feedback:
        return handle(ref.id);
    }
    return {};
}

void Textures::flush(rhi::CommandList& cmd) {
    for (const Upload& u : pending_) cmd.uploadTexture(u.texture, u.rect, std::as_bytes(std::span(u.rgba)));
    pending_.clear();
}

std::size_t Textures::live_count() const {
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.live; }));
}

}  // namespace sh::gpu
