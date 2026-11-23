// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "formats/texture/picture.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

#include "formats/png/sh_png.h"

namespace sh::formats::texture {

namespace {

using R = Result<RgbaImage>;

// A little-endian reader that never leaves the file: a read past the end marks it short and gives 0.
class Bytes {
public:
    explicit Bytes(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    std::size_t size() const { return bytes_.size(); }
    bool has(std::size_t at, std::size_t count) const { return at <= bytes_.size() && count <= bytes_.size() - at; }
    std::uint8_t u8(std::size_t at) const { return bytes_[at]; }
    std::uint16_t u16(std::size_t at) const { return static_cast<std::uint16_t>(bytes_[at] | bytes_[at + 1] << 8); }
    std::uint32_t u32(std::size_t at) const {
        return static_cast<std::uint32_t>(u16(at)) | static_cast<std::uint32_t>(u16(at + 2)) << 16;
    }

private:
    std::span<const std::uint8_t> bytes_;
};

// A block of a TIM: its byte length, then a rectangle (x, y, width, height in 16-bit words), then the words.
constexpr std::size_t kBlockHeader = 12;

void put_colour(std::uint16_t word, std::uint8_t* out) {
    const auto widen = [](unsigned five) { return static_cast<std::uint8_t>(five << 3 | five >> 2); };
    out[0] = widen(word & 0x1F);
    out[1] = widen((word >> 5) & 0x1F);
    out[2] = widen((word >> 10) & 0x1F);
    out[3] = word == 0 ? 0 : 255;
}

}  // namespace

Result<RgbaImage> decode_tim(std::span<const std::uint8_t> file) {
    const Bytes f(file);
    if (!f.has(0, 8) || f.u32(0) != 0x10) return R::fail("not a TIM file");
    const std::uint32_t flags = f.u32(4);
    const unsigned depth = flags & 7;  // 0: 4 bits, 1: 8 bits, 2: 16 bits, 3: 24 bits
    if (depth > 3) return R::fail("a TIM of an unknown colour depth");

    std::size_t at = 8;
    std::size_t palette_at = 0;
    std::size_t palette_width = 0;
    if (flags & 8) {
        if (!f.has(at, kBlockHeader)) return R::fail("a TIM that ends in its palette header");
        const std::uint32_t length = f.u32(at);
        if (length < kBlockHeader || !f.has(at, length)) return R::fail("a TIM whose palette runs past the file");
        palette_width = f.u16(at + 8);
        palette_at = at + kBlockHeader;
        if (palette_width * 2 > length - kBlockHeader) palette_width = (length - kBlockHeader) / 2;
        at += length;
    }
    if (!f.has(at, kBlockHeader)) return R::fail("a TIM that ends in its picture header");
    const std::size_t words = f.u16(at + 8);
    const int height = f.u16(at + 10);
    const std::size_t pixels_at = at + kBlockHeader;
    const std::size_t row_bytes = words * 2;

    constexpr std::size_t kNumerator[] = {4, 2, 1, 2};
    constexpr std::size_t kDenominator[] = {1, 1, 1, 3};
    const int width = static_cast<int>(words * kNumerator[depth] / kDenominator[depth]);
    if (width <= 0 || height <= 0 || width > kPictureSideMax || height > kPictureSideMax) {
        return R::fail("a TIM of " + std::to_string(width) + "x" + std::to_string(height) + " texels");
    }
    if (!f.has(pixels_at, row_bytes * static_cast<std::size_t>(height))) {
        return R::fail("a TIM whose picture runs past the file");
    }
    if (depth < 2 && palette_width == 0) return R::fail("a TIM of palette indices without a palette");

    RgbaImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    std::uint8_t* out = image.pixels.data();
    for (int y = 0; y < height; ++y) {
        const std::size_t row = pixels_at + static_cast<std::size_t>(y) * row_bytes;
        for (std::size_t x = 0; x < static_cast<std::size_t>(width); ++x, out += 4) {
            if (depth == 3) {
                out[0] = f.u8(row + x * 3);
                out[1] = f.u8(row + x * 3 + 1);
                out[2] = f.u8(row + x * 3 + 2);
                out[3] = 255;
            } else if (depth == 2) {
                put_colour(f.u16(row + x * 2), out);
            } else {
                // Two indices to a byte at 4 bits, the low half first. An index the palette row does not have
                // reads its first colour.
                std::size_t index = depth == 0 ? (f.u8(row + x / 2) >> (x % 2 * 4)) & 0xF : f.u8(row + x);
                if (index >= palette_width) index = 0;
                put_colour(f.u16(palette_at + index * 2), out);
            }
        }
    }
    return R::ok(std::move(image));
}

Result<RgbaImage> decode_picture(std::span<const std::uint8_t> file) {
    constexpr std::uint8_t kPngStart[] = {0x89, 'P', 'N', 'G'};
    R result = R::fail("an empty file");
    if (file.size() >= 8 && std::equal(std::begin(kPngStart), std::end(kPngStart), file.begin())) {
        Result<sh::png::Rgba8> png = sh::png::decode_rgba8(file);
        if (!png) return R::fail("PNG: " + png.error());
        RgbaImage image;
        image.width = static_cast<int>(png.value().width);
        image.height = static_cast<int>(png.value().height);
        image.pixels = std::move(png.value().pixels);
        result = R::ok(std::move(image));
    } else if (is_dds(file)) {
        result = decode_bc7(file);
        if (!result) return R::fail("DDS: " + result.error());
    } else {
        result = decode_tim(file);
        if (!result) return result;
    }
    const RgbaImage& image = result.value();
    if (image.width <= 0 || image.height <= 0 || image.width > kPictureSideMax || image.height > kPictureSideMax) {
        return R::fail("a picture of " + std::to_string(image.width) + "x" + std::to_string(image.height) + " pixels");
    }
    return result;
}

}  // namespace sh::formats::texture
