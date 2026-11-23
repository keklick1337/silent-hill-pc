// SPDX-License-Identifier: GPL-3.0-or-later
#include "features/hud/text_panel.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string_view>

#include "stb/include/stb_truetype.h"

namespace sh::hud {

struct PanelFont::Face {
    std::vector<unsigned char> file;  // the font keeps pointing into it
    stbtt_fontinfo info{};
    bool ready = false;

    float advance(unsigned char c, float scale) const {
        int advance_width = 0;
        int left_bearing = 0;
        stbtt_GetCodepointHMetrics(&info, c, &advance_width, &left_bearing);
        return static_cast<float>(advance_width) * scale;
    }

    float width(std::string_view text, float scale) const {
        float sum = 0.0f;
        for (const char c : text) {
            sum += advance(static_cast<unsigned char>(c), scale);
        }
        return sum;
    }
};

PanelFont::PanelFont() : face_(std::make_unique<Face>()) {}
PanelFont::~PanelFont() = default;

bool PanelFont::load(const std::filesystem::path& file) {
    Face& face = *face_;
    face.ready = false;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    face.file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (face.file.empty()) {
        return false;
    }
    const int offset = stbtt_GetFontOffsetForIndex(face.file.data(), 0);
    face.ready = offset >= 0 && stbtt_InitFont(&face.info, face.file.data(), offset) != 0;
    return face.ready;
}

bool PanelFont::ready() const { return face_->ready; }

namespace {

constexpr int kMaxSide = 4096;
// Clear pixels between the outline and the edge of the picture.
constexpr int kMargin = 2;

// A row cut at its tab: the key column and the description, or a heading.
struct Row {
    std::string_view key;
    std::string_view rest;
    bool heading = false;

    explicit Row(std::string_view text) {
        const std::size_t tab = text.find('\t');
        heading = tab == std::string_view::npos;
        if (heading) {
            rest = text;
        } else {
            key = text.substr(0, tab);
            rest = text.substr(tab + 1);
        }
    }
};

// How much of each pixel the glyphs cover, 0..255.
class Coverage {
public:
    Coverage(int width, int height)
        : width_(width), height_(height), cells_(static_cast<std::size_t>(width) * height, 0) {}

    std::uint8_t at(int x, int y) const { return cells_[static_cast<std::size_t>(y) * width_ + x]; }

    // Glyphs that overlap keep the stronger coverage.
    void raise(int x, int y, std::uint8_t value) {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) {
            return;
        }
        std::uint8_t& cell = cells_[static_cast<std::size_t>(y) * width_ + x];
        cell = std::max(cell, value);
    }

    // The strongest coverage within `reach` pixels: where the outline is.
    std::uint8_t strongest_near(int x, int y, int reach) const {
        std::uint8_t best = 0;
        for (int ny = std::max(y - reach, 0); ny <= std::min(y + reach, height_ - 1); ++ny) {
            for (int nx = std::max(x - reach, 0); nx <= std::min(x + reach, width_ - 1); ++nx) {
                best = std::max(best, at(nx, ny));
            }
        }
        return best;
    }

private:
    int width_;
    int height_;
    std::vector<std::uint8_t> cells_;
};

}  // namespace

PanelBitmap PanelFont::render(std::span<const std::string> rows, float pixel_height) const {
    const Face& face = *face_;
    if (!face.ready || rows.empty()) {
        return {};
    }
    const float scale = stbtt_ScaleForPixelHeight(&face.info, pixel_height);
    const int outline = pixel_height >= 22.0f ? 2 : 1;
    const float column_gap = pixel_height * 0.8f;
    const float line_height = pixel_height * 1.25f;
    const int inset = outline + kMargin;

    float key_width = 0.0f;
    float rest_width = 0.0f;
    for (const std::string& text : rows) {
        const Row row(text);
        key_width = std::max(key_width, face.width(row.key, scale));
        rest_width = std::max(rest_width, face.width(row.rest, scale));
    }

    PanelBitmap bitmap;
    bitmap.width = static_cast<int>(key_width + column_gap + rest_width) + 2 * inset;
    bitmap.height = static_cast<int>(line_height * static_cast<float>(rows.size())) + 2 * inset;
    if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.width > kMaxSide || bitmap.height > kMaxSide) {
        return {};
    }

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&face.info, &ascent, &descent, &line_gap);

    Coverage coverage(bitmap.width, bitmap.height);
    std::vector<unsigned char> glyph;
    // A glyph much larger than the text height is not one of the panel's characters.
    const float glyph_limit = pixel_height * 3.0f;

    const auto draw_text = [&](std::string_view text, float pen, float baseline) {
        for (const char raw : text) {
            const unsigned char c = static_cast<unsigned char>(raw);
            int left = 0, top = 0, right = 0, bottom = 0;
            stbtt_GetCodepointBitmapBox(&face.info, c, scale, scale, &left, &top, &right, &bottom);
            const int w = right - left;
            const int h = bottom - top;
            if (w > 0 && h > 0 && static_cast<float>(w) < glyph_limit && static_cast<float>(h) < glyph_limit) {
                glyph.assign(static_cast<std::size_t>(w) * h, 0);
                stbtt_MakeCodepointBitmap(&face.info, glyph.data(), w, h, w, scale, scale, c);
                const int origin_x = static_cast<int>(pen) + left;
                const int origin_y = static_cast<int>(baseline) + top;
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        coverage.raise(origin_x + x, origin_y + y, glyph[static_cast<std::size_t>(y) * w + x]);
                    }
                }
            }
            pen += face.advance(c, scale);
        }
    };

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row row(rows[i]);
        const float baseline =
            static_cast<float>(inset) + line_height * static_cast<float>(i) + static_cast<float>(ascent) * scale;
        if (row.heading) {
            draw_text(row.rest, static_cast<float>(inset), baseline);
        } else {
            draw_text(row.key, static_cast<float>(inset) + key_width - face.width(row.key, scale), baseline);
            draw_text(row.rest, static_cast<float>(inset) + key_width + column_gap, baseline);
        }
    }

    // White text over a black outline, composed into one straight-alpha picture: the colour is the text's share
    // of the combined opacity.
    bitmap.rgba.resize(static_cast<std::size_t>(bitmap.width) * bitmap.height * 4);
    std::uint8_t* out = bitmap.rgba.data();
    for (int y = 0; y < bitmap.height; ++y) {
        for (int x = 0; x < bitmap.width; ++x, out += 4) {
            const float text_alpha = static_cast<float>(coverage.at(x, y)) / 255.0f;
            const float outline_alpha = static_cast<float>(coverage.strongest_near(x, y, outline)) / 255.0f;
            const float alpha = text_alpha + outline_alpha * (1.0f - text_alpha);
            const std::uint8_t white =
                alpha > 0.0f ? static_cast<std::uint8_t>(255.0f * text_alpha / alpha) : std::uint8_t{0};
            out[0] = out[1] = out[2] = white;
            out[3] = static_cast<std::uint8_t>(255.0f * alpha);
        }
    }
    return bitmap;
}

}  // namespace sh::hud
