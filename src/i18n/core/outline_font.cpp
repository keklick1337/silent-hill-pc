// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/core/outline_font.h"

#include "i18n/core/unicode.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string_view>

// A private copy: the exe has other users of the library with their own settings.
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "stb/include/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace sh::i18n {

struct OutlineFont::Impl {
    std::vector<std::uint8_t> bytes;
    stbtt_fontinfo info{};
    int units_per_em = 1000;
    int cap_height = 700;
};

OutlineFont::OutlineFont(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OutlineFont::~OutlineFont() = default;

std::unique_ptr<OutlineFont> OutlineFont::from_bytes(std::vector<std::uint8_t> bytes, std::uint32_t face_offset) {
    auto impl = std::make_unique<Impl>();
    impl->bytes = std::move(bytes);
    if (impl->bytes.size() < 12 || face_offset >= impl->bytes.size() ||
        stbtt_InitFont(&impl->info, impl->bytes.data(), static_cast<int>(face_offset)) == 0) {
        return nullptr;
    }
    const float per_unit = stbtt_ScaleForMappingEmToPixels(&impl->info, 1.0f);
    impl->units_per_em = per_unit > 0.0f ? static_cast<int>(std::lround(1.0f / per_unit)) : 1000;
    impl->cap_height = (impl->units_per_em * 7) / 10;
    std::unique_ptr<OutlineFont> font(new OutlineFont(std::move(impl)));
    const Box h = font->ink_box(U'H');
    if (!h.empty && h.y1 > 0) {
        font->impl_->cap_height = h.y1;
    }
    return font;
}

std::unique_ptr<OutlineFont> OutlineFont::load(const FontFace& face) {
    std::ifstream in(face.file, std::ios::binary);
    if (!in) {
        return nullptr;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return from_bytes(std::move(bytes), face.offset);
}

bool OutlineFont::has(char32_t cp) const { return stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp)) != 0; }

OutlineFont::Box OutlineFont::ink_box(char32_t cp) const {
    Box box;
    const int glyph = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
    if (glyph != 0 && stbtt_GetGlyphBox(&impl_->info, glyph, &box.x0, &box.y0, &box.x1, &box.y1) != 0) {
        box.empty = box.x1 <= box.x0 || box.y1 <= box.y0;
    }
    return box;
}

int OutlineFont::advance(char32_t cp) const {
    int advance = 0;
    int bearing = 0;
    stbtt_GetGlyphHMetrics(&impl_->info, stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp)), &advance, &bearing);
    return advance;
}

int OutlineFont::units_per_em() const { return impl_->units_per_em; }
int OutlineFont::cap_height() const { return impl_->cap_height; }

void OutlineFont::draw(char32_t cp, float scale_x, float scale_y, float origin_x, float baseline_y, std::uint8_t* out,
                       int width, int height) const {
    const int glyph = stbtt_FindGlyphIndex(&impl_->info, static_cast<int>(cp));
    if (glyph == 0) {
        return;
    }
    const float floor_x = std::floor(origin_x);
    const float floor_y = std::floor(baseline_y);
    const float shift_x = origin_x - floor_x;
    const float shift_y = baseline_y - floor_y;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetGlyphBitmapBoxSubpixel(&impl_->info, glyph, scale_x, scale_y, shift_x, shift_y, &x0, &y0, &x1, &y1);
    const int w = x1 - x0;
    const int h = y1 - y0;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        return;
    }
    std::vector<std::uint8_t> bitmap(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    stbtt_MakeGlyphBitmapSubpixel(&impl_->info, bitmap.data(), w, h, w, scale_x, scale_y, shift_x, shift_y, glyph);
    const int left = static_cast<int>(floor_x) + x0;
    const int top = static_cast<int>(floor_y) + y0;
    for (int y = 0; y < h; ++y) {
        const int oy = top + y;
        if (oy < 0 || oy >= height) {
            continue;
        }
        for (int x = 0; x < w; ++x) {
            const int ox = left + x;
            if (ox < 0 || ox >= width) {
                continue;
            }
            std::uint8_t& dst = out[static_cast<std::size_t>(oy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(ox)];
            dst = static_cast<std::uint8_t>(std::min(255, dst + bitmap[static_cast<std::size_t>(y * w + x)]));
        }
    }
}

ScriptGroup script_group(char32_t cp) {
    cp = arabic_base(cp);
    if ((cp >= 0x0600 && cp <= 0x08FF) || (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF)) {
        return ScriptGroup::Arabic;
    }
    if ((cp >= 0x0590 && cp <= 0x05FF) || (cp >= 0xFB1D && cp <= 0xFB4F)) {
        return ScriptGroup::Hebrew;
    }
    return is_wide(cp) ? ScriptGroup::Wide : ScriptGroup::General;
}

FaceFit fit_face(const OutlineFont& font, ScriptGroup group, const CellStyle& style) {
    // Letters that reach the script's highest and lowest points.
    std::u32string_view probes = U"HpgjyQ\u0431\u0434\u0440\u0443\u0446\u0449\u03b2\u03b6\u03be\u03c8";
    switch (group) {
        case ScriptGroup::Arabic: probes = U"اأإلكيعظجمطﻻﻟﻢﻊﻲﻚ"; break;
        case ScriptGroup::Hebrew: probes = U"אלקץךף"; break;
        case ScriptGroup::Wide: probes = U"国語中あア한글"; break;
        case ScriptGroup::General: break;
    }
    FaceFit fit;
    fit.scale = static_cast<float>(style.cap_height) / static_cast<float>(std::max(1, font.cap_height()));
    fit.baseline = static_cast<float>(style.baseline);
    int top = 0;
    int bottom = 0;
    bool any = false;
    for (const char32_t cp : probes) {
        const OutlineFont::Box box = font.ink_box(cp);
        if (box.empty) {
            continue;
        }
        top = any ? std::max(top, box.y1) : box.y1;
        bottom = any ? std::min(bottom, box.y0) : box.y0;
        any = true;
    }
    if (!any || top <= bottom) {
        return fit;
    }
    const float room = static_cast<float>(style.cell_height);
    const float tall = static_cast<float>(top - bottom) * fit.scale;
    if (tall > room) {
        fit.scale *= room / tall;
    }
    const float above = static_cast<float>(top) * fit.scale;
    const float below = static_cast<float>(-bottom) * fit.scale;
    fit.baseline = std::max(fit.baseline, above);
    fit.baseline = std::min(fit.baseline, room - below);
    return fit;
}

CellGlyph render_cell_glyph(const OutlineFont& font, char32_t cp, const FaceFit& fit, const CellStyle& style) {
    CellGlyph out;
    if (!font.has(cp) || fit.scale <= 0.0f) {
        return out;
    }
    const OutlineFont::Box box = font.ink_box(cp);
    const float natural = static_cast<float>(font.advance(cp)) * fit.scale;
    out.mark = is_mark(cp);

    float scale_x = fit.scale;
    float origin_x = 0.0f;
    if (out.mark) {
        // Centred in a cell that is laid over the character before it.
        out.advance = 0;
        origin_x = static_cast<float>(style.cell_width) * 0.5f - static_cast<float>(box.x0 + box.x1) * 0.5f * scale_x;
    } else {
        float step = natural;
        if (step < 0.5f && !box.empty) {
            step = static_cast<float>(box.x1) * scale_x + 1.0f;  // a letter the face gives no advance
        }
        out.advance = std::max(1, static_cast<int>(std::lround(step)));
        // Ideographs, kana and Hangul take one cell each, narrowed like the kanji of the original Japanese release,
        // so a line holds as many of them as its translators counted on.
        const int widest = script_group(cp) == ScriptGroup::Wide ? style.cell_width : style.max_cells * style.cell_width;
        out.advance = std::min(out.advance, widest);
        // The glyph is stretched to the whole pixels it advances, so joined letters meet exactly.
        if (step > 0.0f) {
            scale_x = fit.scale * static_cast<float>(out.advance) / step;
        }
    }
    if (box.empty) {
        return out;  // a space: only the advance
    }
    const float right = origin_x + static_cast<float>(box.x1) * scale_x;
    out.cells = (out.advance > style.cell_width || right > static_cast<float>(style.cell_width) + 0.5f) ? 2 : 1;
    out.cells = std::min(out.cells, style.max_cells);

    const int s = style.scale;
    const int width = out.cells * style.cell_width * s;
    const int height = style.cell_height * s;
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    font.draw(cp, scale_x * static_cast<float>(s), fit.scale * static_cast<float>(s), origin_x * static_cast<float>(s),
              fit.baseline * static_cast<float>(s), coverage.data(), width, height);

    const auto ink = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < width && y < height &&
               coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] >= 128;
    };
    const int last_row = static_cast<int>(style.row_color.size()) - 1;
    out.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    for (int y = 0; y < height; ++y) {
        // The cell rows' colours, blended between row centres so the shading has no bands at this size.
        const float row = (static_cast<float>(y) + 0.5f) / static_cast<float>(s) - 0.5f -
                          static_cast<float>(style.row_offset);
        const int r0 = std::clamp(static_cast<int>(std::floor(row)), 0, last_row);
        const int r1 = std::min(r0 + 1, last_row);
        const float t = std::clamp(row - static_cast<float>(r0), 0.0f, 1.0f);
        for (int x = 0; x < width; ++x) {
            std::uint8_t* px = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
            if (ink(x, y)) {
                for (int c = 0; c < 3; ++c) {
                    const float a = static_cast<float>(style.row_color[static_cast<std::size_t>(r0)][static_cast<std::size_t>(c)]);
                    const float b = static_cast<float>(style.row_color[static_cast<std::size_t>(r1)][static_cast<std::size_t>(c)]);
                    px[c] = static_cast<std::uint8_t>(std::lround(a + (b - a) * t));
                }
                px[3] = 255;
            } else if (ink(x - s, y - s)) {
                px[0] = style.shadow_color[0];
                px[1] = style.shadow_color[1];
                px[2] = style.shadow_color[2];
                px[3] = 255;
            }
        }
    }
    return out;
}

}  // namespace sh::i18n
