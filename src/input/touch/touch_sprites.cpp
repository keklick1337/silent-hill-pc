// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/touch_sprites.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace sh::input::touch {

namespace {

constexpr int kGrid = 4;  // samples per pixel side of an icon

// An even side with `margin` pixels around a shape `extent` pixels across.
int side_for(float extent, int margin) {
    int s = static_cast<int>(std::ceil(std::max(extent, 0.0f))) + 2 * margin;
    return s + (s & 1);
}

std::uint8_t byte_of(float coverage) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(coverage, 0.0f, 1.0f) * 255.0f));
}

// Fills a mask by the distance of each pixel centre from the mask's centre.
template <typename Coverage>
Mask radial(float extent, Coverage coverage) {
    Mask m;
    m.size = side_for(extent, 2);
    m.alpha.assign(static_cast<std::size_t>(m.size) * static_cast<std::size_t>(m.size), 0);
    const float c = 0.5f * static_cast<float>(m.size);
    for (int y = 0; y < m.size; ++y) {
        for (int x = 0; x < m.size; ++x) {
            const float d = std::hypot(static_cast<float>(x) + 0.5f - c, static_cast<float>(y) + 0.5f - c);
            m.alpha[static_cast<std::size_t>(y * m.size + x)] = byte_of(coverage(d));
        }
    }
    return m;
}

float edge(Point a, Point b, Point p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); }

// The icon's samples: 16 bits per pixel, one per sample of the 4x4 grid, set where any shape covers it. Shapes
// overlap (a stroke's joints, a filled area and its outline), so they are united, never summed.
class Samples {
public:
    explicit Samples(int size)
        : size_(size), bits_(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0) {}

    // Sets every sample inside the box (pixels) for which inside(sample point) holds.
    template <typename Inside>
    void cover(float x0, float y0, float x1, float y1, Inside inside) {
        const int px0 = std::max(0, static_cast<int>(std::floor(x0)));
        const int py0 = std::max(0, static_cast<int>(std::floor(y0)));
        const int px1 = std::min(size_ - 1, static_cast<int>(std::floor(x1)));
        const int py1 = std::min(size_ - 1, static_cast<int>(std::floor(y1)));
        for (int y = py0; y <= py1; ++y) {
            for (int x = px0; x <= px1; ++x) {
                std::uint16_t& b = bits_[static_cast<std::size_t>(y * size_ + x)];
                if (b == 0xFFFF) continue;
                for (int sy = 0; sy < kGrid; ++sy) {
                    for (int sx = 0; sx < kGrid; ++sx) {
                        const Point p{static_cast<float>(x) + (static_cast<float>(sx) + 0.5f) / kGrid,
                                      static_cast<float>(y) + (static_cast<float>(sy) + 0.5f) / kGrid};
                        if (inside(p)) b = static_cast<std::uint16_t>(b | (1u << (sy * kGrid + sx)));
                    }
                }
            }
        }
    }

    Mask mask() const {
        Mask m;
        m.size = size_;
        m.alpha.resize(bits_.size());
        for (std::size_t i = 0; i < bits_.size(); ++i)
            m.alpha[i] = static_cast<std::uint8_t>((std::popcount(bits_[i]) * 255 + 8) / 16);
        return m;
    }

private:
    int size_;
    std::vector<std::uint16_t> bits_;
};

}  // namespace

Mask disc_mask(float radius) {
    return radial(2.0f * radius, [radius](float d) { return radius - d + 0.5f; });
}

Mask ring_mask(float radius, float width) {
    const float half = 0.5f * std::max(width, 1.0f);
    // A line thinner than a pixel keeps its weight as a fainter line.
    const float weight = std::min(width, 1.0f);
    return radial(2.0f * (radius + half), [=](float d) { return weight * (half - std::fabs(d - radius) + 0.5f); });
}

Mask halo_mask(float radius, float spread) {
    const float s = std::max(spread, 1.0f);
    return radial(2.0f * (radius + s), [=](float d) {
        const float t = std::clamp((d - radius) / s, 0.0f, 1.0f);
        const float fade = 1.0f - t * t * (3.0f - 2.0f * t);
        return fade * fade;
    });
}

Mask icon_mask(const VectorIcon& icon, float side) {
    float overhang = 0.0f;
    for (const VectorIcon::Stroke& s : icon.strokes) overhang = std::max(overhang, s.width);
    for (const VectorIcon::Ring& r : icon.rings) overhang = std::max(overhang, r.width);
    const float k = icon.box_size > 0.0f ? side / icon.box_size : 1.0f;
    const int size = side_for(side + overhang * k, 2);
    const float c = 0.5f * static_cast<float>(size);
    const float half = 0.5f * icon.box_size;
    const auto at = [&](Point q) {
        return Point{c + (q.x - icon.box_x - half) * k, c + (q.y - icon.box_y - half) * k};
    };

    Samples samples(size);
    for (const auto& t : icon.triangles) {
        const Point a = at(t[0]), b = at(t[1]), d = at(t[2]);
        const float area = edge(a, b, d);
        if (area == 0.0f) continue;
        const float sign = area > 0.0f ? 1.0f : -1.0f;
        samples.cover(std::min({a.x, b.x, d.x}), std::min({a.y, b.y, d.y}), std::max({a.x, b.x, d.x}),
                      std::max({a.y, b.y, d.y}), [&](Point p) {
                          return sign * edge(a, b, p) >= 0.0f && sign * edge(b, d, p) >= 0.0f &&
                                 sign * edge(d, a, p) >= 0.0f;
                      });
    }
    // A stroke is a capsule per segment: its round ends are the joints of a polyline.
    const auto segment = [&](Point a, Point b, float w) {
        const float r = 0.5f * w;
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float len2 = dx * dx + dy * dy;
        samples.cover(std::min(a.x, b.x) - r, std::min(a.y, b.y) - r, std::max(a.x, b.x) + r, std::max(a.y, b.y) + r,
                      [&](Point p) {
                          float t = len2 > 0.0f ? ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2 : 0.0f;
                          t = std::clamp(t, 0.0f, 1.0f);
                          const float ex = p.x - (a.x + t * dx), ey = p.y - (a.y + t * dy);
                          return ex * ex + ey * ey <= r * r;
                      });
    };
    for (const VectorIcon::Stroke& stroke : icon.strokes) {
        const std::size_t n = stroke.points.size();
        const float w = stroke.width * k;
        if (n == 1) segment(at(stroke.points[0]), at(stroke.points[0]), w);
        for (std::size_t i = 0; i + 1 < n; ++i) segment(at(stroke.points[i]), at(stroke.points[i + 1]), w);
        if (stroke.closed && n > 2) segment(at(stroke.points[n - 1]), at(stroke.points[0]), w);
    }
    for (const VectorIcon::Ring& ring : icon.rings) {
        const Point o = at(ring.centre);
        const float r = ring.radius * k;
        const float w = 0.5f * ring.width * k;
        const float reach = r + w;
        samples.cover(o.x - reach, o.y - reach, o.x + reach, o.y + reach, [&](Point p) {
            const float d = std::hypot(p.x - o.x, p.y - o.y);
            return ring.width <= 0.0f ? d <= r : std::fabs(d - r) <= w;
        });
    }
    return samples.mask();
}

}  // namespace sh::input::touch
