// The RGBA read-back to VRAM word packer (render/offscreen).

#include "framework/sh_test.h"
#include "render/offscreen/rgba_to_vram.h"

#include <cstdint>
#include <vector>

namespace r = sh::render;

SH_TEST(rgba_to_vram_packs_555_with_stp_bit) {
    SH_CHECK(r::RgbaToVram::PackPixel(0) == 0);
    SH_CHECK(r::RgbaToVram::PackPixel(0x00F8F8F8u) == 0xFFFF);
    SH_CHECK(r::RgbaToVram::PackPixel(0x00000008u) == ((1u << 10) | 0x8000u));
    SH_CHECK(r::RgbaToVram::PackPixel(0x00070707u) == 0);
}

SH_TEST(rgba_to_vram_flips_rows_and_rejects_bad_rects) {
    std::vector<std::uint16_t> vram(r::RgbaToVram::kVramWidth * r::RgbaToVram::kVramHeight, 0);
    const std::uint32_t src[4] = {0x00000008u, 0x00000008u, 0x00080000u, 0x00080000u};  // 2x2
    SH_CHECK(r::RgbaToVram::Copy(vram.data(), src, 10, 20, 2, 2, true));
    SH_CHECK(vram[20 * 1024 + 10] == ((1u << 0) | 0x8000u));
    SH_CHECK(vram[21 * 1024 + 10] == ((1u << 10) | 0x8000u));
    SH_CHECK(!r::RgbaToVram::Copy(vram.data(), src, 1023, 0, 2, 2, false));
    SH_CHECK(!r::RgbaToVram::Copy(vram.data(), src, 0, 511, 2, 2, false));
}
