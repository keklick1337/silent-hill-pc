// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/core/bitmap_font.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace sh::i18n {
namespace {

constexpr char kMagic[4] = {'S', 'H', 'B', 'F'};
constexpr std::size_t kHeaderBytes = 12;
constexpr int kMaxSide = 64;

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (i * 8)));
    }
}

}  // namespace

Result<BitmapFont> BitmapFont::parse(std::vector<std::uint8_t> bytes) {
    using R = Result<BitmapFont>;
    if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), kMagic, sizeof kMagic) != 0) {
        return R::fail("not a bitmap font (no SHBF header)");
    }
    const int version = bytes[4] | (bytes[5] << 8);
    BitmapFont font;
    font.width_ = bytes[6];
    font.height_ = bytes[7];
    font.count_ = le32(bytes.data() + 8);
    if (version != 1 || font.width_ < 1 || font.height_ < 1 || font.width_ > kMaxSide || font.height_ > kMaxSide) {
        return R::fail("unsupported bitmap font version or glyph size");
    }
    font.row_bytes_ = static_cast<std::size_t>(font.width_ + 7) / 8;
    const std::size_t glyph_bytes = font.row_bytes_ * static_cast<std::size_t>(font.height_);
    if (font.count_ > (bytes.size() - kHeaderBytes) / (4 + glyph_bytes)) {
        return R::fail("the bitmap font is shorter than its glyph count");
    }
    for (std::size_t i = 1; i < font.count_; ++i) {
        if (le32(bytes.data() + kHeaderBytes + i * 4) <= le32(bytes.data() + kHeaderBytes + (i - 1) * 4)) {
            return R::fail("the code points of the bitmap font are not ascending");
        }
    }
    font.bytes_ = std::move(bytes);
    return R::ok(std::move(font));
}

Result<BitmapFont> BitmapFont::load(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return Result<BitmapFont>::fail(file.string() + ": cannot be read");
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto font = parse(std::move(bytes));
    return font ? std::move(font) : Result<BitmapFont>::fail(file.string() + ": " + font.error());
}

std::vector<std::uint8_t> BitmapFont::write(int width, int height,
                                            const std::map<char32_t, std::vector<std::uint8_t>>& glyphs) {
    std::vector<std::uint8_t> out(kMagic, kMagic + sizeof kMagic);
    out.push_back(1);
    out.push_back(0);
    out.push_back(static_cast<std::uint8_t>(width));
    out.push_back(static_cast<std::uint8_t>(height));
    put32(out, static_cast<std::uint32_t>(glyphs.size()));
    for (const auto& [cp, rows] : glyphs) {
        put32(out, static_cast<std::uint32_t>(cp));
    }
    const std::size_t glyph_bytes = static_cast<std::size_t>((width + 7) / 8) * static_cast<std::size_t>(height);
    for (const auto& [cp, rows] : glyphs) {
        std::vector<std::uint8_t> padded = rows;
        padded.resize(glyph_bytes, 0);
        out.insert(out.end(), padded.begin(), padded.end());
    }
    return out;
}

const std::uint8_t* BitmapFont::glyph(char32_t cp) const {
    std::size_t lo = 0;
    std::size_t hi = count_;
    const std::uint8_t* codes = bytes_.data() + kHeaderBytes;
    while (lo < hi) {
        const std::size_t mid = lo + (hi - lo) / 2;
        const std::uint32_t at = le32(codes + mid * 4);
        if (at == cp) {
            return codes + count_ * 4 + mid * row_bytes_ * static_cast<std::size_t>(height_);
        }
        if (at < cp) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return nullptr;
}

bool BitmapFont::pixel(const std::uint8_t* glyph, int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) {
        return false;
    }
    const std::uint8_t byte = glyph[static_cast<std::size_t>(y) * row_bytes_ + static_cast<std::size_t>(x / 8)];
    return ((byte >> (7 - (x % 8))) & 1) != 0;
}

CellGlyph render_bitmap_glyph(const BitmapFont& font, char32_t cp, const CellStyle& style) {
    CellGlyph out;
    const std::uint8_t* rows = font.glyph(cp);
    if (rows == nullptr) {
        return out;
    }
    const int cw = style.cell_width;
    const int ch = style.cell_height;
    // One game pixel of the cell: any source pixel that falls on it.
    const int fit_w = std::min(font.width(), cw);
    const int left = (cw - fit_w) / 2;
    const int base_h = static_cast<int>(style.row_color.size());
    // Centred on the game font's rows, which a taller cell holds from row_offset down.
    const int top = font.height() <= base_h ? style.row_offset + (base_h - font.height()) / 2
                                            : std::max(0, (ch - font.height()) / 2);
    std::vector<std::uint8_t> ink(static_cast<std::size_t>(cw * ch), 0);
    for (int y = 0; y < std::min(font.height(), ch); ++y) {
        for (int x = 0; x < fit_w; ++x) {
            const int from = (x * font.width()) / fit_w;
            const int to = std::max(from + 1, ((x + 1) * font.width()) / fit_w);
            bool on = false;
            for (int sx = from; sx < to; ++sx) {
                on = on || font.pixel(rows, sx, y);
            }
            ink[static_cast<std::size_t>((top + y) * cw + left + x)] = on ? 1 : 0;
        }
    }

    out.cells = 1;
    out.advance = cw;
    const int s = style.scale;
    const int width = cw * s;
    out.rgba.assign(static_cast<std::size_t>(width * ch * s * 4), 0);
    const int last_row = static_cast<int>(style.row_color.size()) - 1;
    const auto at = [&](int x, int y) { return x >= 0 && y >= 0 && x < cw && y < ch && ink[static_cast<std::size_t>(y * cw + x)] != 0; };
    for (int y = 0; y < ch; ++y) {
        for (int x = 0; x < cw; ++x) {
            const bool on = at(x, y);
            if (!on && !at(x - 1, y - 1)) {
                continue;
            }
            const std::array<std::uint8_t, 3>& color =
                on ? style.row_color[static_cast<std::size_t>(std::clamp(y - style.row_offset, 0, last_row))] : style.shadow_color;
            for (int py = 0; py < s; ++py) {
                for (int px = 0; px < s; ++px) {
                    std::uint8_t* p = &out.rgba[static_cast<std::size_t>(((y * s + py) * width + x * s + px) * 4)];
                    p[0] = color[0];
                    p[1] = color[1];
                    p[2] = color[2];
                    p[3] = 255;
                }
            }
        }
    }
    return out;
}

}  // namespace sh::i18n
