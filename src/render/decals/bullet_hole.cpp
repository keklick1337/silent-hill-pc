// SPDX-License-Identifier: GPL-3.0-or-later
// The bullet holes' sheet -- see bullet_hole.h.

#include "render/decals/bullet_hole.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sh::render {
namespace {

constexpr float kPi = 3.14159265358979f;

std::uint32_t mix(std::uint32_t v) {
    v ^= v >> 16;
    v *= 0x7FEB352Du;
    v ^= v >> 15;
    v *= 0x846CA68Bu;
    v ^= v >> 16;
    return v;
}

class Random {
public:
    explicit Random(std::uint32_t seed) : state_(seed) {}
    float unit() {
        state_ = mix(state_ + 0x9E3779B9u);
        return static_cast<float>(state_ >> 8) * (1.0f / 16777216.0f);
    }
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }

private:
    std::uint32_t state_;
};

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float lattice(std::uint32_t seed, int x, int y) {
    const std::uint32_t h = mix(seed ^ mix(static_cast<std::uint32_t>(x) * 0x1F123BB5u ^
                                           static_cast<std::uint32_t>(y) * 0x5F356495u));
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

// Smooth value noise, 0..1.
float noise(std::uint32_t seed, float x, float y) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const float tx = smoothstep(0.0f, 1.0f, x - fx);
    const float ty = smoothstep(0.0f, 1.0f, y - fy);
    const float a = lattice(seed, ix, iy) + (lattice(seed, ix + 1, iy) - lattice(seed, ix, iy)) * tx;
    const float b = lattice(seed, ix, iy + 1) + (lattice(seed, ix + 1, iy + 1) - lattice(seed, ix, iy + 1)) * tx;
    return a + (b - a) * ty;
}

struct Crack {
    float angle = 0.f;
    float length = 0.f;
    float width = 0.f;
    float bend = 0.f;
};

// The shape of one hole: lengths are fractions of the cell's half side.
struct Shape {
    std::uint32_t seed = 0;
    float core = 0.f;                 // radius of the punched hole
    float rim = 0.f;                  // mean radius of the chipped crater
    std::array<float, 16> chips{};    // the crater's radius factor around the hole
    std::array<Crack, 5> cracks{};
    int crack_count = 0;
    float halo = 0.f;                 // how far the soot reaches past the crater
};

Shape make_shape(std::uint32_t seed) {
    Random r(seed);
    Shape s;
    s.seed = mix(seed ^ 0xA5A5A5A5u);
    s.core = r.range(0.10f, 0.15f);
    s.rim = r.range(0.24f, 0.36f);
    // Plaster breaks off in flakes: a few directions lose much more than their neighbours.
    for (float& c : s.chips) {
        c = r.range(0.72f, 1.05f);
    }
    const int flakes = 1 + static_cast<int>(r.unit() * 3.0f);
    for (int i = 0; i < flakes; ++i) {
        const auto at = static_cast<std::size_t>(r.unit() * 16.0f) & 15u;
        s.chips[at] = r.range(1.35f, 1.9f);
        s.chips[(at + 1) & 15u] = std::max(s.chips[(at + 1) & 15u], r.range(1.1f, 1.5f));
    }
    s.crack_count = 2 + static_cast<int>(r.unit() * 3.99f);
    for (int i = 0; i < s.crack_count; ++i) {
        Crack& c = s.cracks[static_cast<std::size_t>(i)];
        c.angle = r.range(0.0f, 2.0f * kPi);
        c.length = r.range(0.45f, 0.88f);
        c.width = r.range(0.012f, 0.024f);
        c.bend = r.range(-0.5f, 0.5f);
    }
    s.halo = r.range(0.22f, 0.4f);
    return s;
}

// The crater's radius toward `angle`.
float rim_at(const Shape& s, float angle) {
    const float turn = angle / (2.0f * kPi);
    const float at = (turn - std::floor(turn)) * 16.0f;
    const auto i = static_cast<std::size_t>(at) & 15u;
    const float t = smoothstep(0.0f, 1.0f, at - std::floor(at));
    const float factor = s.chips[i] + (s.chips[(i + 1) & 15u] - s.chips[i]) * t;
    return std::clamp(s.rim * factor, s.core + 0.05f, 0.66f);
}

struct Texel {
    float height = 0.f;  // 0 = the wall's surface, -1 = the bottom of the hole
    float albedo = 0.f;
    float alpha = 0.f;
};

// Layer `top` over what `under` already holds.
void over(Texel& under, float albedo, float alpha) {
    const float out = alpha + under.alpha * (1.0f - alpha);
    if (out > 1e-5f) {
        under.albedo = (albedo * alpha + under.albedo * under.alpha * (1.0f - alpha)) / out;
    }
    under.alpha = out;
}

Texel texel_at(const Shape& s, float x, float y) {
    Texel t;
    const float r = std::sqrt(x * x + y * y);
    const float angle = std::atan2(y, x);
    const float rim = rim_at(s, angle);
    const float grain = noise(s.seed, x * 9.0f + 31.0f, y * 9.0f + 17.0f);
    const float coarse = noise(s.seed ^ 0x51u, x * 3.5f + 5.0f, y * 3.5f + 11.0f);

    // Soot and dust thrown around the impact.
    const float reach = rim + s.halo * (0.75f + 0.5f * coarse);
    const float soot = 1.0f - smoothstep(rim * 0.8f, reach, r);
    over(t, 0.10f, 0.42f * soot * soot * (0.7f + 0.3f * grain));

    // Hairline cracks running out of the crater.
    for (int i = 0; i < s.crack_count; ++i) {
        const Crack& c = s.cracks[static_cast<std::size_t>(i)];
        const float along = x * std::cos(c.angle) + y * std::sin(c.angle);
        if (along <= 0.0f || along >= c.length) {
            continue;
        }
        const float wander = c.bend * along * along + 0.03f * (noise(s.seed + 7u * static_cast<std::uint32_t>(i),
                                                                     along * 14.0f, 0.5f) - 0.5f);
        const float across = std::fabs(-x * std::sin(c.angle) + y * std::cos(c.angle) - wander);
        const float width = c.width * (1.0f - along / c.length) + 0.004f;
        const float line = 1.0f - smoothstep(width * 0.4f, width, across);
        if (line > 0.0f) {
            over(t, 0.05f, 0.8f * line);
            t.height = std::min(t.height, -0.25f * line);
        }
    }

    // The crater: paint and plaster knocked off down to the bare material, deeper toward the hole.
    const float crater = 1.0f - smoothstep(rim - 0.025f, rim + 0.015f, r);
    if (crater > 0.0f) {
        const float inward = std::clamp(1.0f - (r - s.core) / std::max(rim - s.core, 1e-3f), 0.0f, 1.0f);
        const float depth = std::pow(inward, 0.8f) * 0.55f + 0.12f * (coarse - 0.5f) * (1.0f - inward) + 0.06f;
        t.height = t.height * (1.0f - crater) - depth * crater;
        const float bare = (0.46f - 0.26f * inward) * (0.8f + 0.4f * grain);
        over(t, bare * (1.0f - 0.35f * depth), 0.9f * crater);
    } else {
        // The paint curls up a little where it tore.
        const float lip = (r - rim) / 0.05f;
        t.height += 0.07f * std::exp(-lip * lip);
    }

    // The hole itself.
    const float hole = 1.0f - smoothstep(s.core * 0.82f, s.core * 1.12f, r);
    if (hole > 0.0f) {
        t.height = t.height * (1.0f - hole) - hole;
        over(t, 0.015f, hole);
    }

    // Nothing reaches the cell's edge.
    t.alpha *= 1.0f - smoothstep(0.84f, 0.97f, std::max(std::fabs(x), std::fabs(y)));
    t.alpha *= 1.0f - smoothstep(0.88f, 1.0f, r);
    return t;
}

std::uint8_t byte_of(float v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

void draw_cell(std::vector<std::uint8_t>& sheet, int cell_x, int cell_y, const Shape& shape) {
    constexpr int n = kBulletHoleCell;
    // One texel of margin around the cell for the height's slope.
    std::vector<Texel> texels(static_cast<std::size_t>((n + 2) * (n + 2)));
    const auto at = [&](int x, int y) -> Texel& {
        return texels[static_cast<std::size_t>((y + 1) * (n + 2) + (x + 1))];
    };
    for (int y = -1; y <= n; ++y) {
        for (int x = -1; x <= n; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(n) * 2.0f - 1.0f;
            const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(n) * 2.0f - 1.0f;
            at(x, y) = texel_at(shape, fx, fy);
        }
    }
    // How steep the relief reads: the tilt of a unit height step between neighbouring texels.
    constexpr float kRelief = 3.0f;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const Texel& t = at(x, y);
            float nx = -(at(x + 1, y).height - at(x - 1, y).height) * kRelief;
            float ny = -(at(x, y + 1).height - at(x, y - 1).height) * kRelief;
            const float length = std::sqrt(nx * nx + ny * ny + 1.0f);
            nx /= length;
            ny /= length;
            const std::size_t o =
                static_cast<std::size_t>(((cell_y * n + y) * kBulletHoleSheet + cell_x * n + x) * 4);
            sheet[o + 0] = byte_of(nx * 0.5f + 0.5f);
            sheet[o + 1] = byte_of(ny * 0.5f + 0.5f);
            sheet[o + 2] = byte_of(t.albedo);
            sheet[o + 3] = byte_of(t.alpha);
        }
    }
}

}  // namespace

std::vector<std::uint8_t> bullet_hole_sheet(std::uint32_t seed) {
    std::vector<std::uint8_t> sheet(static_cast<std::size_t>(kBulletHoleSheet * kBulletHoleSheet * 4));
    for (int i = 0; i < kBulletHoleVariants; ++i) {
        draw_cell(sheet, i % kBulletHoleGrid, i / kBulletHoleGrid,
                  make_shape(mix(seed + 0x632BE5ABu * static_cast<std::uint32_t>(i + 1))));
    }
    return sheet;
}

BulletHoleLook bullet_hole_look(std::int32_t x, std::int32_t y, std::int32_t z) {
    const std::uint32_t h = mix(static_cast<std::uint32_t>(x) * 0x9E3779B1u ^
                                mix(static_cast<std::uint32_t>(y) * 0x85EBCA77u ^
                                    mix(static_cast<std::uint32_t>(z) * 0xC2B2AE3Du)));
    BulletHoleLook look;
    look.variant = static_cast<int>(h % static_cast<std::uint32_t>(kBulletHoleVariants));
    look.angle = static_cast<float>((h >> 4) & 0xFFFu) * (2.0f * kPi / 4096.0f);
    look.scale = 0.8f + static_cast<float>((h >> 16) & 0xFFu) * (0.55f / 255.0f);
    look.mirrored = ((h >> 24) & 1u) != 0;
    return look;
}

}  // namespace sh::render
