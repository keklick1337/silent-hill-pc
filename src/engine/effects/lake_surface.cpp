// SPDX-License-Identifier: GPL-3.0-or-later
// The ending lake and the ending's 2D pieces (lake_surface.h).

#include "engine/effects/lake_surface.h"
#include "engine/effects/effect_view.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace sh::effects {

namespace {

constexpr int kGrid = kLakeCells + 1;
constexpr std::uint32_t kRingHalf = 0x80000;   // a ring past this has broken: it fades out and starts the next
constexpr std::uint32_t kRingEnd = 0x100000;
constexpr std::uint16_t kGlowPalette = 0x030E;

// The ripple words keep the original's two layouts: a ring phase is [ring][a + b * 16].
std::uint32_t& ring(std::uint32_t* r, int k, int a, int b) { return r[(k << 8) + a + b * 16]; }
std::uint32_t& speed(std::uint32_t* r, int a, int b) { return r[kLakeRings * 256 + a + b * 16]; }

std::uint16_t tpage_word(int tp, int abr, int x, int y) {
    return static_cast<std::uint16_t>(((tp & 0x3) << 7) | ((abr & 0x3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3ff) >> 6) |
                                      ((y & 0x200) << 2));
}

std::int32_t mul12_wide(std::int64_t a, std::int64_t b) { return static_cast<std::int32_t>((a * b) >> 12); }

int nibble(const std::uint8_t* row, int j) { return (row[j >> 1] >> ((j & 1) * 4)) & 0xF; }

draw3d::Polygon flat_textured(const std::int32_t x[4], const std::int32_t y[4], const std::uint32_t uv[4]) {
    draw3d::Polygon q;
    q.count = 4;
    q.textured = true;
    for (int k = 0; k < 4; k++) {
        q.v[k].xy = {static_cast<std::int16_t>(x[k]), static_cast<std::int16_t>(y[k])};
        q.v[k].uv = {static_cast<std::uint8_t>(uv[k]), static_cast<std::uint8_t>(uv[k] >> 8)};
    }
    q.palette = draw3d::Palette::from_word(static_cast<std::uint16_t>(uv[0] >> 16));
    q.page = draw3d::TexturePage::from_word(static_cast<std::uint16_t>(uv[1] >> 16));
    q.v[0].color = {0x80, 0x80, 0x80};
    return q;
}

}  // namespace

void LakeSurface::blur(const std::uint32_t* in, std::uint32_t* out) {
    // Three rows of the page around the one being written (above, this, below), 128 bytes each.
    std::uint8_t window[3][128];
    std::memset(window[0], 0, sizeof window[0]);
    std::memcpy(window[1], in, 128);
    std::memcpy(window[2], in + 32, 128);

    for (int i = 0; i < 256; i++) {
        std::uint32_t* dst = out + (i << 5);
        std::uint32_t word = 0;
        for (int j = 0; j < 256; j++) {
            int sum = 0;
            if (j > 0) sum = nibble(window[1], j - 1);
            if (j < 255) sum += nibble(window[1], j + 1);
            if (i > 0) {
                sum += nibble(window[0], j);
                if (j > 0) sum += nibble(window[0], j - 1);
                if (j < 255) sum += nibble(window[0], j + 1);
            }
            if (i < 255) {
                sum += nibble(window[2], j);
                if (j > 0) sum += nibble(window[2], j - 1);
                if (j < 255) sum += nibble(window[2], j + 1);
            }
            word |= static_cast<std::uint32_t>((sum + 7) >> 3) << ((j & 7) * 4);
            if ((j & 7) == 7) {
                *dst++ = word;
                word = 0;
            }
        }
        if (i != 255) {
            std::memcpy(window[0], window[1], 128);
            std::memcpy(window[1], window[2], 128);
            if (i == 254) {
                std::memset(window[2], 0, 128);
            } else {
                std::memcpy(window[2], in + ((i + 2) << 5), 128);
            }
        }
    }
}

void LakeSurface::build_pages(LakeHost& host, const std::array<std::uint8_t, 8>& pages, int first,
                              std::uint32_t* buffers[2]) {
    for (int i = first; i < static_cast<int>(pages.size()) - 1; i++) {
        std::uint32_t* out = buffers[(i & 1) == 0];
        blur(buffers[i & 1], out);
        host.upload_page(pages[static_cast<std::size_t>(i + 1)], out);
    }
}

void LakeSurface::seed(LakeHost& host, std::uint32_t* ripples) {
    for (int i = 0; i < kLakeCells; i++) {
        for (int j = 0; j < kLakeCells; j++) {
            const std::int32_t r = host.rand16();
            const int k = r & 0xF;
            if (k < kLakeRings) {
                const std::uint32_t phase = static_cast<std::uint32_t>((r >> 4) & 0x7FFFF);
                ring(ripples, k, i, j) = phase;
                if (k > 0) ring(ripples, k - 1, i, j) = phase + kRingHalf;
            }
            speed(ripples, i, j) = kRingEnd;
        }
    }
}

void LakeSurface::draw(LakeHost& host, std::uint32_t* ripples, const std::array<std::uint8_t, 8>& pages) {
    host.begin_view();
    host.screen_glow();

    std::uint8_t busy[kGrid][kGrid] = {};
    std::uint8_t shade[kGrid][kGrid] = {};
    ScreenPoint at[kGrid][kGrid] = {};
    const std::int32_t dt = host.delta_time();

    for (int k = 0; k < kLakeRings; k++) {
        // Advance the rings: a rising ring that breaks the half way starts the next ring (15 in 16), a falling
        // one fades to nothing.
        for (int i = 1; i < kLakeCells; i++) {
            for (int j = 1; j < kLakeCells; j++) {
                std::int32_t phase = static_cast<std::int32_t>(ring(ripples, k, j, i));
                if (phase <= 0) {
                    shade[j][i] = 0;
                    continue;
                }
                const std::int32_t step = mul12_wide(dt, speed(ripples, i, j));
                busy[j][i]++;
                if (phase > static_cast<std::int32_t>(kRingHalf)) {
                    phase += step;
                    if (phase > static_cast<std::int32_t>(kRingEnd)) {
                        ring(ripples, k, j, i) = 0;
                        shade[j][i] = 0;
                    } else {
                        ring(ripples, k, j, i) = static_cast<std::uint32_t>(phase);
                        shade[j][i] = static_cast<std::uint8_t>(-(phase >> 12));
                    }
                } else {
                    phase += step;
                    if (k < kLakeRings - 1 && phase > static_cast<std::int32_t>(kRingHalf)) {
                        if (host.rand16() & 0xF) ring(ripples, k + 1, j, i)++;
                        shade[j][i] = static_cast<std::uint8_t>(-(phase >> 12));
                    } else {
                        shade[j][i] = static_cast<std::uint8_t>(phase >> 12);
                    }
                    ring(ripples, k, j, i) = static_cast<std::uint32_t>(phase);
                }
            }
            shade[16][i] = 0;
            shade[0][i] = 0;
        }
        for (int j = 0; j < kGrid; j++) {
            shade[j][16] = 0;
            shade[j][0] = 0;
        }

        // The ring's sheet, a step lower for each ring; the last two points are projected one by one.
        const auto point = [&](int n) {
            return Vec3s{static_cast<std::int16_t>(((n / kGrid) << 7) - 0x400), static_cast<std::int16_t>(-8 * k),
                         static_cast<std::int16_t>(0x400 - ((n % kGrid) << 7))};
        };
        for (int n = 0; n < kGrid * kGrid - 2; n += 3) {
            const Vec3s v[3] = {point(n), point(n + 1), point(n + 2)};
            ScreenPoint out[3];
            host.project3(v, out);
            for (int m = 0; m < 3; m++) at[(n + m) / kGrid][(n + m) % kGrid] = out[m];
        }
        for (int n = kGrid * kGrid - 2; n < kGrid * kGrid; n++) at[n / kGrid][n % kGrid] = host.project(point(n));

        const std::int32_t page = pages[static_cast<std::size_t>(k)];
        const std::uint16_t tpage = tpage_word(0, 1, page << 6, ((page >> 4) & 1) << 8);
        for (int i = 0; i < kLakeCells; i++) {
            for (int j = 0; j < kLakeCells; j++) {
                const ScreenPoint* c[4] = {&at[i][j], &at[i + 1][j], &at[i][j + 1], &at[i + 1][j + 1]};
                const std::int32_t depth = (c[0]->depth + c[1]->depth + c[2]->depth + c[3]->depth) >> 2;
                if (depth < 65 || ((depth - 64) >> 3) >= 2048) continue;
                // Only drawing follows: the box may follow the frame (effect_view.h).
                const std::int32_t half = screen_box_half_width();
                if (std::abs(c[0]->x) > half && std::abs(c[1]->x) > half && std::abs(c[2]->x) > half &&
                    std::abs(c[3]->x) > half) {
                    continue;
                }
                if (std::abs(c[0]->y) > 160 && std::abs(c[1]->y) > 160 && std::abs(c[2]->y) > 160 &&
                    std::abs(c[3]->y) > 160) {
                    continue;
                }
                const std::uint8_t g[4] = {shade[i][j], shade[i + 1][j], shade[i][j + 1], shade[i + 1][j + 1]};
                if (g[0] < 4 && g[1] < 4 && g[2] < 4 && g[3] < 4) continue;

                const std::uint32_t uv[4] = {
                    static_cast<std::uint32_t>((i << 4) + ((j << 4) << 8)) + (static_cast<std::uint32_t>(kGlowPalette) << 16),
                    static_cast<std::uint32_t>(0xF + (i << 4) + ((j << 4) << 8)) + (static_cast<std::uint32_t>(tpage) << 16),
                    static_cast<std::uint32_t>((i << 4) + ((0xF + 0x10 * j) << 8)) & 0xFFFFu,
                    static_cast<std::uint32_t>(0xF + i * 16 + ((0xF + 0x10 * j) << 8)) & 0xFFFFu};
                draw3d::Polygon q;
                q.count = 4;
                q.textured = true;
                q.gouraud = true;
                q.shade = {true, false};
                for (int m = 0; m < 4; m++) {
                    q.v[m].xy = {c[m]->x, c[m]->y};
                    q.v[m].color = {g[m], g[m], g[m]};
                    q.v[m].uv = {static_cast<std::uint8_t>(uv[m]), static_cast<std::uint8_t>(uv[m] >> 8)};
                }
                q.palette = draw3d::Palette::from_word(static_cast<std::uint16_t>(uv[0] >> 16));
                q.page = draw3d::TexturePage::from_word(static_cast<std::uint16_t>(uv[1] >> 16));
                host.emit(q, (depth - 0x40) >> 3);
            }
        }
    }

    // Quiet cells start a new ripple now and then (about one frame time in 32768).
    for (int i = 1; i < kLakeCells; i++) {
        for (int j = 1; j < kLakeCells; j++) {
            if (busy[j][i] != 0) continue;
            if ((host.rand16() & 0x7FFF) <= dt) {
                ring(ripples, 0, j, i)++;
                speed(ripples, j, i) = static_cast<std::uint32_t>((host.rand16() & 0x7FFFF) + 0x100000);
            }
        }
    }
}

draw3d::Polygon UfoEnding::sprite(const EndingSprite& s, int index, const EndingLook& look) {
    const std::int32_t page = look.sprite_page + (index == 4 ? 1 : 0);
    std::int32_t x = static_cast<std::int16_t>(s.x);
    std::int32_t y = s.y;
    if (index == 4) {
        x = static_cast<std::int16_t>(s.x + look.jitter_x);
        y = s.y + look.jitter_y;
    }
    const std::int32_t xs[4] = {x, static_cast<std::int16_t>(x + s.w), x, static_cast<std::int16_t>(x + s.w)};
    const std::int32_t ys[4] = {y, y, y + s.h, y + s.h};
    const std::uint32_t uv[4] = {
        static_cast<std::uint32_t>(s.u + (s.v << 8)) + (static_cast<std::uint32_t>(look.palette) << 16),
        static_cast<std::uint32_t>(s.u + s.w + (s.v << 8)) +
            (static_cast<std::uint32_t>(tpage_word(1, 0, page << 6, ((page >> 4) & 1) << 8)) << 16),
        static_cast<std::uint32_t>(s.u + ((s.v + s.h) << 8)) & 0xFFFFu,
        static_cast<std::uint32_t>(s.u + s.w + ((s.v + s.h) << 8)) & 0xFFFFu};
    return flat_textured(xs, ys, uv);
}

std::array<draw3d::Polygon, 2> UfoEnding::slide_in(std::int16_t progress, const EndingLook& look) {
    const std::int32_t top = look.jitter_y + mul12_wide(progress, 76) - 88;
    const std::int32_t bottom = look.jitter_y - 8;
    std::array<draw3d::Polygon, 2> out;
    for (int i = 0; i < 2; i++) {
        // The left half slides in from the left, the right half from the right.
        std::int32_t left, right, page, u, w;
        if (i == 0) {
            left = look.jitter_x + (mul12_wide(progress, 167) - 160);
            right = look.jitter_x + 18;
            page = look.title_page;
            u = 0;
            w = 0xB2;
        } else {
            left = look.jitter_x + 18;
            right = look.jitter_x - (mul12_wide(progress, 135) - 160);
            page = look.title_page + 1;
            u = 50;
            w = 142;
        }
        const std::int32_t xs[4] = {left, right, left, right};
        const std::int32_t ys[4] = {top, top, bottom, bottom};
        const std::uint32_t uv[4] = {
            static_cast<std::uint32_t>(u) + (static_cast<std::uint32_t>(look.palette) << 16),
            static_cast<std::uint32_t>(u + w) +
                (static_cast<std::uint32_t>(tpage_word(1, 0, page << 6, ((page >> 4) & 1) << 8)) << 16),
            static_cast<std::uint32_t>(u + (80 << 8)) & 0xFFFFu, static_cast<std::uint32_t>(u + w + (80 << 8)) & 0xFFFFu};
        out[static_cast<std::size_t>(i)] = flat_textured(xs, ys, uv);
    }
    return out;
}

}  // namespace sh::effects
