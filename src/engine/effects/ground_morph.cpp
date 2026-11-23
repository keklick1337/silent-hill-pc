// SPDX-License-Identifier: GPL-3.0-or-later
// The ground morph of map6_s00 (ground_morph.h).

#include "engine/effects/ground_morph.h"

#include <algorithm>

namespace sh::effects {

namespace {

constexpr int kN = kGroundMorphGrid;
constexpr int kLooks = 4;
// The patch of street the grid covers (geometry units): x from -20 m, z from -8 m going down.
constexpr std::int32_t kGridX = -0x1400;
constexpr std::int32_t kGridZ = -0x800;
// The light of a point is taken on a flat ground facing up, under this offset.
constexpr Vec3s kLightNormal{0, -4096, 0};
constexpr Vec3 kLightOffset{-3072, 0, 61440};
// A texture cell of the loading-screen pages.
constexpr std::int32_t kCell = 0x2F;

struct Look {
    std::uint8_t look = 0;     // 0..3
    std::int32_t blend = 0;    // 0..128: weight of this look against the next one
};

// Where the level stands in a point's band: the band is cut in quarters, one per look, and the last quarter
// fades out over its first half.
Look look_of(const GroundMorphBand& band, std::int32_t level) {
    if (level < band.start * 4) return {0, 0x80};
    if (level >= band.end * 4) return {3, 0};
    const std::int32_t t = level - band.start * 4;
    std::int32_t span = (band.end - band.start) * 4;
    const std::int32_t quarter = span >> 2;
    if (t < quarter) return {0, ((quarter - t) << 7) / quarter};
    const std::int32_t half = span >> 1;
    if (t < half) return {1, ((half - t) << 7) / (half - quarter)};
    const std::int32_t three = (span * 3) >> 2;
    if (t < three) return {2, ((three - t) << 7) / (three - half)};
    span -= (span - three) >> 1;
    return {3, span < t ? 0 : ((span - t) << 7) / (span - three)};
}

std::uint8_t scale(std::uint8_t c, std::int32_t w) {
    return static_cast<std::uint8_t>(std::min((c * w) >> 7, 0xFF));
}

std::uint32_t uv_word(std::int32_t u, std::int32_t v, std::int32_t extra) {
    return static_cast<std::uint32_t>(u + (v << 8) + (extra << 16));
}

}  // namespace

void GroundMorph::draw(GroundMorphHost& host, const GroundMorphTable& table, std::int32_t level) {
    const Vec3 player = host.player_position();
    const Vec3 metres{player.x >> 12, player.y >> 12, player.z >> 12};
    const Vec3 origin{metres.x << 8, metres.y << 8, metres.z << 8};
    const std::int32_t screen = host.begin_view(Vec3{metres.x << 12, metres.y << 12, metres.z << 12});

    const auto point = [&](int n) {
        return Vec3s{static_cast<std::int16_t>((((n % kN) << 8) + kGridX) - origin.x),
                     static_cast<std::int16_t>(-origin.y),
                     static_cast<std::int16_t>((kGridZ - origin.z) - ((n / kN) << 8))};
    };
    std::array<ScreenPoint, kN * kN> at{};
    for (int n = 0; n < kN * kN - 1; n += 3) {
        const Vec3s v[3] = {point(n), point(n + 1), point(n + 2)};
        host.project3(v, &at[static_cast<std::size_t>(n)]);
    }
    at[kN * kN - 1] = host.project(point(kN * kN - 1));

    std::array<std::array<Look, kN>, kN> looks{};
    std::array<std::array<std::array<Rgb, 2>, kN>, kN> shades{};
    for (int i = 0; i < kN; i++) {
        for (int j = 0; j < kN; j++) {
            const Look l = look_of(table[static_cast<std::size_t>(j)][static_cast<std::size_t>(i)], level);
            looks[i][j] = l;
            const Vec3s pos{static_cast<std::int16_t>((j << 8) - 0x800), 0, static_cast<std::int16_t>(0x800 - (i << 8))};
            const Rgb lit = host.light(pos, kLightNormal, kLightOffset);
            shades[i][j][0] = {scale(lit.r, l.blend), scale(lit.g, l.blend), scale(lit.b, l.blend)};
            shades[i][j][1] = {scale(lit.r, 0x80 - l.blend), scale(lit.g, 0x80 - l.blend), scale(lit.b, 0x80 - l.blend)};
        }
    }

    for (int i = 0; i < kN - 1; i++) {
        for (int j = 0; j < kN - 1; j++) {
            const std::int32_t depth = (at[i * kN + j].depth + at[(i + 1) * kN + j].depth + at[i * kN + j + 1].depth +
                                        at[(i + 1) * kN + j + 1].depth) >> 2;
            if (depth + 0x80 < screen || ((depth + 0x80) >> 3) >= 0x800) continue;

            // Look 4 is the Otherworld's floor seams over the last look.
            for (int look = 0; look <= kLooks; look++) {
                std::array<Rgb, 4> c{};
                bool any = false;
                for (int a = 0; a < 2; a++) {
                    for (int b = 0; b < 2; b++) {
                        const int corner = looks[i + a][j + b].look;
                        Rgb rgb{};
                        if (corner == look) {
                            rgb = shades[i + a][j + b][0];
                        } else if (corner + 1 == look) {
                            rgb = shades[i + a][j + b][1];
                        }
                        c[static_cast<std::size_t>(a * 2 + b)] = rgb;
                        any = any || rgb.r != 0 || rgb.g != 0 || rgb.b != 0;
                    }
                }
                if (!any) continue;

                const std::int32_t page = (5 + (look >= 2) + (look >= 3)) | 0x20;
                const std::int32_t palette = host.palette(look);
                std::int32_t u = 0;
                std::int32_t v = 0;
                std::int32_t du = kCell;
                std::int32_t dv = kCell;
                std::int32_t row_class = 0;
                std::int32_t col_class = 0;
                if (look == kLooks) {
                    // Edge, inner or opposite edge of the 8 x 8 seam pattern, by row and column.
                    row_class = ((i & 7) > 0) + ((i & 7) == 7);
                    col_class = ((j & 7) > 0) + ((j & 7) == 7);
                    if (row_class == 1) {
                        u = col_class == 1 ? 0 : 0x90;
                    } else {
                        u = col_class == 1 ? 0x30 : 0x60;
                    }
                    if (row_class >= 2) {
                        dv = -kCell;
                        v += kCell;
                    }
                    if (col_class <= 0) {
                        du = -kCell;
                        u += kCell;
                    }
                } else if (look != 0) {
                    // A 4 x 4 tile of cells, mirrored every four cells.
                    if (j & 4) {
                        u = 0xBF - (j & 3) * 0x30;
                        du = -kCell;
                    } else {
                        u = (j & 3) * 3 * 0x10;
                    }
                    if (i & 4) {
                        v = (i & 3) * 0x30 + 0x40;
                    } else {
                        v = 0xFF - (i & 3) * 0x30;
                        dv = -kCell;
                    }
                }

                const ScreenPoint* corner[4];
                int shade[4];
                std::uint32_t uv[4];
                if (look == kLooks && row_class != col_class && row_class != 1 && col_class != 1) {
                    // The seam's corner pieces are turned a quarter.
                    corner[0] = &at[i * kN + j + 1];
                    corner[1] = &at[i * kN + j];
                    corner[2] = &at[(i + 1) * kN + j + 1];
                    corner[3] = &at[(i + 1) * kN + j];
                    shade[0] = 1;
                    shade[1] = 0;
                    shade[2] = 3;
                    shade[3] = 2;
                    uv[0] = uv_word(u + du, v, palette);
                    uv[1] = uv_word(u, v, page);
                    uv[2] = uv_word(u + du, v + dv, 0) & 0xFFFFu;
                    uv[3] = uv_word(u, v + dv, 0) & 0xFFFFu;
                } else {
                    corner[0] = &at[i * kN + j];
                    corner[1] = &at[i * kN + j + 1];
                    corner[2] = &at[(i + 1) * kN + j];
                    corner[3] = &at[(i + 1) * kN + j + 1];
                    shade[0] = 0;
                    shade[1] = 1;
                    shade[2] = 2;
                    shade[3] = 3;
                    uv[0] = uv_word(u, v, palette);
                    uv[1] = uv_word(u + du, v, page);
                    uv[2] = uv_word(u, v + dv, 0) & 0xFFFFu;
                    uv[3] = uv_word(u + du, v + dv, 0) & 0xFFFFu;
                }

                draw3d::Polygon q;
                q.count = 4;
                q.textured = true;
                q.gouraud = true;
                q.shade = {true, false};
                for (int k = 0; k < 4; k++) {
                    q.v[k].xy = {corner[k]->x, corner[k]->y};
                    q.v[k].color = draw3d::Rgb{c[static_cast<std::size_t>(shade[k])].r, c[static_cast<std::size_t>(shade[k])].g,
                                               c[static_cast<std::size_t>(shade[k])].b};
                    q.v[k].uv = {static_cast<std::uint8_t>(uv[k]), static_cast<std::uint8_t>(uv[k] >> 8)};
                }
                q.palette = draw3d::Palette::from_word(static_cast<std::uint16_t>(uv[0] >> 16));
                q.page = draw3d::TexturePage::from_word(static_cast<std::uint16_t>(uv[1] >> 16));
                host.emit(q, (depth + 0x80) >> 3);
            }
        }
    }
}

}  // namespace sh::effects
