// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/console/font_atlas.h"

// STATIC: the exe already carries a non-static stb_truetype implementation (ui/stb_truetype_impl.cpp).
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/include/stb_truetype.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>

namespace sh::console {

namespace {

struct CodeRange {
    char32_t first, last;
};

constexpr CodeRange kRanges[] = {
    {0x0020, 0x007E}, {0x00A0, 0x00FF}, {0x0100, 0x017F}, {0x0391, 0x03C9}, {0x0400, 0x04FF},
    {0x2013, 0x2014}, {0x2018, 0x201D}, {0x2022, 0x2022}, {0x2026, 0x2026}, {0x2190, 0x2193},
    {0x2500, 0x257F}, {0x25B2, 0x25B2}, {0x25B6, 0x25B6}, {0x25BC, 0x25BC}, {0x25C0, 0x25C0},
    {0x2588, 0x2588},
};

constexpr std::uint32_t kMaxAtlas = 4096;

// A missing glyph is marked with a negative size; blanks have zero size.
bool is_missing(const Glyph& g) { return g.w < 0; }

struct Bitmap {
    std::size_t slot = 0;  // index into glyphs_
    int w = 0, h = 0, x = 0, y = 0;
    std::vector<std::uint8_t> data;
};

}  // namespace

struct FontAtlasBuilder {
    static Result<FontAtlas> run(std::span<const std::uint8_t> ttf, float pixel_height) {
        if (ttf.empty() || !(pixel_height >= 4.0f))
            return Result<FontAtlas>::fail("font atlas: empty font or pixel height too small");
        const unsigned char* data = ttf.data();
        const int offset = stbtt_GetFontOffsetForIndex(data, 0);
        stbtt_fontinfo info;
        if (offset < 0 || !stbtt_InitFont(&info, data, offset))
            return Result<FontAtlas>::fail("font atlas: not a valid TrueType font");

        FontAtlas a;
        a.pixel_height_ = pixel_height;
        const float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, 'M', &adv, &lsb);
        a.cell_w_ = std::max(1.0f, std::round(static_cast<float>(adv) * scale));
        a.cell_h_ = std::ceil(static_cast<float>(asc - desc + gap) * scale);
        a.ascent_ = std::round(static_cast<float>(asc) * scale);

        std::size_t total = 0;
        for (const CodeRange& r : kRanges) {
            a.ranges_.push_back({r.first, r.last, total});
            total += r.last - r.first + 1;
        }
        Glyph missing;
        missing.w = missing.h = -1;
        a.glyphs_.assign(total, missing);

        std::vector<Bitmap> bitmaps;
        for (const auto& r : a.ranges_) {
            for (char32_t cp = r.first; cp <= r.last; ++cp) {
                const std::size_t slot = r.offset + (cp - r.first);
                const int gi = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
                if (gi == 0) continue;
                int x0, y0, x1, y1;
                stbtt_GetGlyphBitmapBox(&info, gi, scale, scale, &x0, &y0, &x1, &y1);
                Glyph g;
                g.w = static_cast<float>(x1 - x0);
                g.h = static_cast<float>(y1 - y0);
                g.x = static_cast<float>(x0);
                g.y = a.ascent_ + static_cast<float>(y0);
                if (x1 <= x0 || y1 <= y0) {
                    a.glyphs_[slot] = Glyph{};  // blank, present
                    continue;
                }
                a.glyphs_[slot] = g;
                Bitmap b;
                b.slot = slot;
                b.w = x1 - x0;
                b.h = y1 - y0;
                b.data.assign(static_cast<std::size_t>(b.w) * b.h, 0);
                stbtt_MakeGlyphBitmap(&info, b.data.data(), b.w, b.h, b.w, scale, scale, gi);
                bitmaps.push_back(std::move(b));
            }
        }

        // Shelf packing with 1px padding; the solid 3x3 block goes first. The atlas grows until it fits.
        std::uint32_t width = 128, height = 0;
        int solid_x = 0, solid_y = 0;
        bool fits = false;
        for (; width <= kMaxAtlas && !fits; width *= 2) {
            std::uint32_t cx = 0, cy = 0, row_h = 0;
            auto place = [&](int w, int h, int& ox, int& oy) {
                const std::uint32_t pw = static_cast<std::uint32_t>(w) + 2, ph = static_cast<std::uint32_t>(h) + 2;
                if (pw > width) return false;
                if (cx + pw > width) {
                    cy += row_h;
                    cx = 0;
                    row_h = 0;
                }
                ox = static_cast<int>(cx) + 1;
                oy = static_cast<int>(cy) + 1;
                cx += pw;
                row_h = std::max(row_h, ph);
                return true;
            };
            bool ok = place(3, 3, solid_x, solid_y);
            for (Bitmap& b : bitmaps) ok = ok && place(b.w, b.h, b.x, b.y);
            if (!ok) continue;
            height = cy + row_h;
            if (height <= width) fits = true;
            if (fits) break;
        }
        if (!fits) return Result<FontAtlas>::fail("font atlas: glyphs do not fit in 4096x4096");
        a.width_ = width;
        a.height_ = height;
        a.pixels_.assign(static_cast<std::size_t>(width) * height, 0);

        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x) a.pixels_[static_cast<std::size_t>(solid_y + y) * width + solid_x + x] = 255;
        const float fw = static_cast<float>(width), fh = static_cast<float>(height);
        Glyph solid;
        solid.u0 = (solid_x + 1.0f) / fw;
        solid.u1 = (solid_x + 2.0f) / fw;
        solid.v0 = (solid_y + 1.0f) / fh;
        solid.v1 = (solid_y + 2.0f) / fh;
        solid.w = solid.h = 3;
        a.solid_ = solid;

        for (const Bitmap& b : bitmaps) {
            for (int y = 0; y < b.h; ++y)
                std::copy_n(b.data.data() + static_cast<std::size_t>(y) * b.w, b.w,
                            a.pixels_.begin() + static_cast<std::size_t>(b.y + y) * width + b.x);
            Glyph& g = a.glyphs_[b.slot];
            g.u0 = static_cast<float>(b.x) / fw;
            g.v0 = static_cast<float>(b.y) / fh;
            g.u1 = static_cast<float>(b.x + b.w) / fw;
            g.v1 = static_cast<float>(b.y + b.h) / fh;
        }
        return Result<FontAtlas>::ok(std::move(a));
    }
};

Result<FontAtlas> FontAtlas::build(std::span<const std::uint8_t> ttf, float pixel_height) {
    return FontAtlasBuilder::run(ttf, pixel_height);
}

bool FontAtlas::has(char32_t cp) const {
    for (const Range& r : ranges_)
        if (cp >= r.first && cp <= r.last) return !is_missing(glyphs_[r.offset + (cp - r.first)]);
    return false;
}

const Glyph& FontAtlas::glyph(char32_t cp) const {
    static const Glyph blank;
    if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return blank;
    if (!has(cp)) cp = U'?';
    for (const Range& r : ranges_)
        if (cp >= r.first && cp <= r.last) {
            const Glyph& g = glyphs_[r.offset + (cp - r.first)];
            return is_missing(g) ? blank : g;
        }
    return blank;
}

std::vector<std::filesystem::path> font_candidates(const std::filesystem::path& configured,
                                                   const std::filesystem::path& bundled) {
    std::vector<std::filesystem::path> all;
    if (!configured.empty()) all.push_back(configured);
    all.emplace_back("C:/Windows/Fonts/consola.ttf");
    all.emplace_back("/System/Library/Fonts/Menlo.ttc");
    all.emplace_back("/Library/Fonts/Menlo.ttc");
    if (!bundled.empty()) all.push_back(bundled);
    all.emplace_back("/usr/share/fonts/TTF/DejaVuSansMono.ttf");
    all.emplace_back("/usr/share/fonts/dejavu/DejaVuSansMono.ttf");
    all.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf");
    std::vector<std::filesystem::path> found;
    for (const auto& p : all) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) found.push_back(p);
    }
    return found;
}

Result<std::vector<std::uint8_t>> load_first_font(const std::vector<std::filesystem::path>& candidates,
                                                  std::filesystem::path* used) {
    for (const auto& p : candidates) {
        std::ifstream in(p, std::ios::binary);
        if (!in) continue;
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.empty()) continue;
        stbtt_fontinfo info;
        const int offset = stbtt_GetFontOffsetForIndex(bytes.data(), 0);
        if (offset < 0 || !stbtt_InitFont(&info, bytes.data(), offset)) continue;
        if (used) *used = p;
        return Result<std::vector<std::uint8_t>>::ok(std::move(bytes));
    }
    return Result<std::vector<std::uint8_t>>::fail("no loadable console font among the candidates");
}

char32_t next_code_point(const std::string& s, std::size_t& pos) {
    const auto at = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    const unsigned char c = at(pos);
    if (c < 0x80) {
        ++pos;
        return c;
    }
    std::size_t extra;
    char32_t cp, min;
    if (c >= 0xC2 && c <= 0xDF) {
        extra = 1;
        cp = c & 0x1F;
        min = 0x80;
    } else if (c >= 0xE0 && c <= 0xEF) {
        extra = 2;
        cp = c & 0x0F;
        min = 0x800;
    } else if (c >= 0xF0 && c <= 0xF4) {
        extra = 3;
        cp = c & 0x07;
        min = 0x10000;
    } else {
        ++pos;
        return 0xFFFD;
    }
    if (pos + extra >= s.size()) {
        ++pos;
        return 0xFFFD;
    }
    for (std::size_t i = 1; i <= extra; ++i) {
        const unsigned char cc = at(pos + i);
        if ((cc & 0xC0) != 0x80) {
            ++pos;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        ++pos;
        return 0xFFFD;
    }
    pos += extra + 1;
    return cp;
}

}  // namespace sh::console
