// Converts a read-back RGBA8 render target into packed 15-bit VRAM words.
#pragma once

#include <cstdint>

namespace sh::render {

class RgbaToVram {
public:
    static constexpr int kVramWidth = 1024;
    static constexpr int kVramHeight = 512;

    // One RGBA8 pixel (R in the low byte) to a 5-5-5 word. The top bit is set when any
    // channel survives the 5-bit truncation, so pure black stays transparent.
    static std::uint16_t PackPixel(std::uint32_t rgba) noexcept;

    // Packs a w*h block into vram at (x, y). flipY mirrors the rows (GL render targets are
    // bottom-up). Returns false (and writes nothing) if the rect is empty or outside VRAM.
    static bool Copy(std::uint16_t* vram, const std::uint32_t* src, int x, int y, int w, int h,
                     bool flipY) noexcept;
};

} // namespace sh::render
