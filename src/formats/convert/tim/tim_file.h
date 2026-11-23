#pragma once

#include "formats/common/status.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// The PS1 TIM file as the game stores it: header, optional CLUT block, pixel block, and whatever follows.
namespace sh::convert::tim {

enum class Depth : std::uint8_t { Bpp4 = 0, Bpp8 = 1, Bpp16 = 2, Bpp24 = 3 };

struct Clut {
    std::uint16_t x = 0;       // VRAM position, in 16-bit words
    std::uint16_t y = 0;
    std::uint16_t width = 0;   // words per row
    std::uint16_t height = 0;  // rows
    std::vector<std::uint16_t> words;  // width * height, row by row
};

struct TimFile {
    Depth depth = Depth::Bpp4;
    bool has_clut = false;
    Clut clut;
    std::uint16_t image_x = 0;      // VRAM position of the pixel block, in 16-bit words
    std::uint16_t image_y = 0;
    std::uint16_t image_words = 0;  // stored width: 16-bit words per row
    std::uint16_t image_height = 0;
    std::vector<std::uint8_t> data;  // image_words * 2 * image_height bytes, as stored
    std::vector<std::uint8_t> tail;  // bytes after the pixel block, kept as they are
};

constexpr unsigned bits_per_pixel(Depth depth) {
    constexpr unsigned kBits[] = {4, 8, 16, 24};
    return kBits[static_cast<unsigned>(depth)];
}

// Width of the picture in pixels. 24-bit rows hold 2/3 pixel per word; a width that does not divide gives 0.
constexpr unsigned pixel_width(Depth depth, unsigned stored_words) {
    switch (depth) {
        case Depth::Bpp4: return stored_words * 4;
        case Depth::Bpp8: return stored_words * 2;
        case Depth::Bpp16: return stored_words;
        case Depth::Bpp24: return stored_words * 2 % 3 == 0 ? stored_words * 2 / 3 : 0;
    }
    return 0;
}

// Stored width for a picture width, or 0 when this depth cannot hold it (4-bit: a multiple of 4, 8-bit and
// 24-bit: even).
constexpr unsigned stored_words(Depth depth, unsigned pixels) {
    switch (depth) {
        case Depth::Bpp4: return pixels % 4 == 0 ? pixels / 4 : 0;
        case Depth::Bpp8: return pixels % 2 == 0 ? pixels / 2 : 0;
        case Depth::Bpp16: return pixels;
        case Depth::Bpp24: return pixels % 2 == 0 ? pixels * 3 / 2 : 0;
    }
    return 0;
}

// Reads the file. Accepts only what write_tim() writes back to the same bytes: magic 0x10, the flags word without
// reserved bits, block lengths equal to 12 + 2 * width * height, a non-empty picture. Everything after the pixel
// block goes to `tail`.
Result<TimFile> parse_tim(std::span<const std::uint8_t> raw);

std::vector<std::uint8_t> write_tim(const TimFile& tim);

}  // namespace sh::convert::tim
