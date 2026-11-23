// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "formats/texture/bc7_dds.h"

#include <algorithm>
#include <array>
#include <cstring>

// The vendored decoder, private to this file; only its BC7 part is used.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define BCDEC_IMPLEMENTATION
#define BCDEC_STATIC
#include "bcdec/include/bcdec.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace sh::formats::texture {

namespace {

// Byte offsets inside the file: the four magic bytes, the 124-byte header, then the 20-byte DX10 extension.
constexpr std::size_t kHeaderAt = 4;
constexpr std::size_t kHeaderBytes = 124;
constexpr std::size_t kExtensionAt = kHeaderAt + kHeaderBytes;
constexpr std::size_t kBlocksAt = kExtensionAt + 20;

constexpr std::size_t kHeightAt = kHeaderAt + 8;
constexpr std::size_t kWidthAt = kHeaderAt + 12;
constexpr std::size_t kLevelsAt = kHeaderAt + 24;
constexpr std::size_t kPixelFlagsAt = kHeaderAt + 76;
constexpr std::size_t kFourCcAt = kHeaderAt + 80;

constexpr std::uint32_t kPixelFlagFourCc = 0x4;
constexpr std::uint32_t kFormatBc7 = 98;
constexpr std::uint32_t kFormatBc7Srgb = 99;

constexpr std::size_t kBlockBytes = 16;
constexpr int kBlockSide = 4;

std::uint32_t word(std::span<const std::uint8_t> file, std::size_t at) {
    return static_cast<std::uint32_t>(file[at]) | static_cast<std::uint32_t>(file[at + 1]) << 8 |
           static_cast<std::uint32_t>(file[at + 2]) << 16 | static_cast<std::uint32_t>(file[at + 3]) << 24;
}

constexpr std::uint32_t four_cc(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16 |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24;
}

constexpr std::uint32_t kMagic = four_cc('D', 'D', 'S', ' ');
constexpr std::uint32_t kExtended = four_cc('D', 'X', '1', '0');

// The formats a texture mod is likely to ship by mistake, by the number of the DX10 extension.
struct NamedFormat {
    std::uint32_t first, last;
    const char* name;
};
constexpr NamedFormat kOtherFormats[] = {
    {28, 29, "uncompressed R8G8B8A8"},
    {87, 88, "uncompressed B8G8R8A8"},
    {70, 72, "BC1 (DXT1)"},
    {73, 75, "BC2 (DXT3)"},
    {76, 78, "BC3 (DXT5)"},
    {79, 81, "BC4"},
    {82, 84, "BC5"},
    {94, 96, "BC6H"},
};

std::string format_name(std::uint32_t code, std::uint32_t number) {
    if (code != kExtended) {
        std::string name = "four-character code '";
        for (int shift = 0; shift < 32; shift += 8) {
            const char c = static_cast<char>((code >> shift) & 0xFF);
            name += (c >= 32 && c < 127) ? c : '?';
        }
        return name + "'";
    }
    for (const NamedFormat& f : kOtherFormats) {
        if (number >= f.first && number <= f.last) return f.name;
    }
    return "format " + std::to_string(number);
}

}  // namespace

bool is_dds(std::span<const std::uint8_t> file) { return file.size() >= 4 && word(file, 0) == kMagic; }

Result<Bc7Picture> read_bc7(std::span<const std::uint8_t> file) {
    using R = Result<Bc7Picture>;
    if (file.size() < kExtensionAt || !is_dds(file) || word(file, kHeaderAt) != kHeaderBytes) {
        return R::fail("not a DDS file");
    }
    const std::uint32_t code = (word(file, kPixelFlagsAt) & kPixelFlagFourCc) ? word(file, kFourCcAt) : 0;
    const std::uint32_t number = (code == kExtended && file.size() >= kBlocksAt) ? word(file, kExtensionAt) : 0;
    if (code != kExtended || (number != kFormatBc7 && number != kFormatBc7Srgb)) {
        return R::fail(format_name(code, number) + " is not readable, only BC7 (formats 98 and 99) is");
    }
    Bc7Picture p;
    p.height = static_cast<int>(word(file, kHeightAt));
    p.width = static_cast<int>(word(file, kWidthAt));
    p.levels = std::max(1, static_cast<int>(word(file, kLevelsAt)));
    p.srgb = number == kFormatBc7Srgb;
    p.blocks = file.subspan(kBlocksAt);
    if (p.width <= 0 || p.height <= 0 || p.blocks.empty()) {
        return R::fail("an empty BC7 picture");
    }
    return R::ok(p);
}

Result<RgbaImage> decode_bc7(std::span<const std::uint8_t> file) {
    using R = Result<RgbaImage>;
    const Result<Bc7Picture> header = read_bc7(file);
    if (!header) return R::fail(header.error());
    const Bc7Picture& p = header.value();

    const std::size_t across = (static_cast<std::size_t>(p.width) + kBlockSide - 1) / kBlockSide;
    const std::size_t down = (static_cast<std::size_t>(p.height) + kBlockSide - 1) / kBlockSide;
    if (across * down * kBlockBytes > p.blocks.size()) {
        return R::fail("a " + std::to_string(p.width) + "x" + std::to_string(p.height) + " BC7 picture needs " +
                       std::to_string(across * down * kBlockBytes) + " bytes of blocks, the file has " +
                       std::to_string(p.blocks.size()));
    }

    RgbaImage image;
    image.width = p.width;
    image.height = p.height;
    const std::size_t row_bytes = static_cast<std::size_t>(p.width) * 4;
    image.pixels.resize(row_bytes * static_cast<std::size_t>(p.height));

    // Every block expands to 4x4 texels; the blocks of the right and bottom edges of a picture whose side is not a
    // multiple of four hold padding, so each block goes through a small square and only its visible part is kept.
    std::array<std::uint8_t, kBlockSide * kBlockSide * 4> texels{};
    for (std::size_t by = 0; by < down; ++by) {
        for (std::size_t bx = 0; bx < across; ++bx) {
            // The decoder reads a block as two 64-bit words; in the file it sits at any byte.
            alignas(8) std::array<std::uint8_t, kBlockBytes> block;
            std::memcpy(block.data(), p.blocks.data() + (by * across + bx) * kBlockBytes, kBlockBytes);
            bcdec_bc7(block.data(), texels.data(), kBlockSide * 4);
            const std::size_t x = bx * kBlockSide;
            const std::size_t y = by * kBlockSide;
            const std::size_t keep_w = std::min<std::size_t>(kBlockSide, static_cast<std::size_t>(p.width) - x);
            const std::size_t keep_h = std::min<std::size_t>(kBlockSide, static_cast<std::size_t>(p.height) - y);
            for (std::size_t row = 0; row < keep_h; ++row) {
                std::memcpy(image.pixels.data() + (y + row) * row_bytes + x * 4, texels.data() + row * kBlockSide * 4,
                            keep_w * 4);
            }
        }
    }
    return R::ok(std::move(image));
}

}  // namespace sh::formats::texture
