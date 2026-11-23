#include "render/offscreen/rgba_to_vram.h"

#include <cstddef>

namespace sh::render {

std::uint16_t RgbaToVram::PackPixel(std::uint32_t rgba) noexcept
{
    // Channels sit at bits 3.., 11.. and 19.. of the little-endian word the render target
    // produces (blue first), the top five bits of each byte.
    const std::uint32_t b = (rgba >> 3) & 0x1F;
    const std::uint32_t g = (rgba >> 11) & 0x1F;
    const std::uint32_t r = (rgba >> 19) & 0x1F;
    const std::uint32_t stp = (r | g | b) ? 1u : 0u;
    return static_cast<std::uint16_t>(r | (g << 5) | (b << 10) | (stp << 15));
}

bool RgbaToVram::Copy(std::uint16_t* vram, const std::uint32_t* src, int x, int y, int w, int h,
                      bool flipY) noexcept
{
    if (!vram || !src || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > kVramWidth ||
        y + h > kVramHeight)
        return false;

    for (int row = 0; row < h; ++row) {
        const int srcRow = flipY ? (h - row - 1) : row;
        const std::uint32_t* s = src + static_cast<std::size_t>(srcRow) * static_cast<std::size_t>(w);
        std::uint16_t* d = vram + static_cast<std::size_t>(y + row) * kVramWidth + x;
        for (int col = 0; col < w; ++col)
            d[col] = PackPixel(s[col]);
    }
    return true;
}

} // namespace sh::render
