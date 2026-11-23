#pragma once

#include "formats/common/status.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sh::png {

// PNG types, in the numbering of the standard (IHDR colour type).
enum class ColorType : std::uint8_t { Gray = 0, Rgb = 2, Indexed = 3, GrayAlpha = 4, Rgba = 6 };

constexpr std::size_t channels(ColorType type) {
    switch (type) {
        case ColorType::Gray:
        case ColorType::Indexed:
            return 1;
        case ColorType::GrayAlpha:
            return 2;
        case ColorType::Rgb:
            return 3;
        case ColorType::Rgba:
            return 4;
    }
    return 0;
}

// An 8-bit image with the layout of the pixel samples as they are in a PNG, without filters or packing.
struct Image {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    ColorType color_type = ColorType::Rgba;
    // height rows of width * channels(color_type) bytes. For Indexed this is the palette index of each pixel.
    // decode() unpacks 1, 2 and 4-bit samples to one byte each and does not scale them (a 4-bit gray 15 stays 15).
    std::vector<std::uint8_t> pixels;
    // Indexed only: R, G, B per entry, 1..256 entries.
    std::vector<std::uint8_t> palette;
    // Indexed only: the tRNS alpha of the first entries. Entries past the end are opaque.
    std::vector<std::uint8_t> palette_alpha;
};

// Writes a PNG: 8 bits per sample, no interlace, filter chosen per row (minimum sum of absolute values),
// one IDAT with a zlib stream at miniz level `level` (9 unless a caller trades size for speed). The output is the
// same for the same input. Fails on a size that does not match the pixel count, an empty palette, or an index past
// the palette.
Result<std::vector<std::uint8_t>> encode(const Image& image, int level = 9);

struct Palette {
    std::vector<std::uint8_t> rgb;    // R, G, B per entry, 1..256 entries
    std::vector<std::uint8_t> alpha;  // tRNS, like Image::palette_alpha
};

// The same indexed picture with several palettes: one PNG per palette, in order. The pixels are compressed once and
// the IDAT is shared by all files, so a texture with dozens of palettes costs one compression.
// `image` must be Indexed; its own palette fields are ignored. Fails if an index is past the end of any palette.
Result<std::vector<std::vector<std::uint8_t>>> encode_palettes(const Image& image, std::span<const Palette> palettes);

// Reads a PNG with its own chunk parser: signature, chunk order, CRC-32 of every chunk, the zlib stream with its
// Adler-32, the five filters. Keeps Indexed images indexed, which stb_image does not do.
// Not supported (an error, use decode_rgba8): 16-bit samples, Adam7 interlace.
// The colour key of a Gray or Rgb image (tRNS) is not applied.
Result<Image> decode(std::span<const std::uint8_t> png);

struct Rgba8 {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;  // width * height * 4, R, G, B, A
};

// Any PNG expanded to RGBA by stb_image: palette, tRNS, gray, 16 bits, interlace. For checks and for the engine's
// reader of textures. Independent of decode().
Result<Rgba8> decode_rgba8(std::span<const std::uint8_t> png);

}  // namespace sh::png
