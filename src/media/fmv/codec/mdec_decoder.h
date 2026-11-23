// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The picture decoder of the disc's movies. A movie frame is one bitstream of 16-bit little-endian words read
// most significant bit first: a 4-word header (two words of preamble, the quantiser scale, the version) and then
// the macroblocks, column by column, each as six 8x8 coefficient blocks (Cr, Cb, four luma) coded with the
// MPEG-1 intra coefficient codes (ISO/IEC 11172-2, table B.14). Written from the published description of the
// console's stream format; the output is a packed RGB24 picture, top row first.

#include <cstddef>
#include <cstdint>
#include <span>

namespace sh::fmv {

class MdecDecoder {
public:
    // Decodes into `rgb` (width * height * 3 bytes). Only whole 16x16 macroblocks are written; the rest of `rgb`
    // keeps what it held. False, with `rgb` untouched, when the header is refused: fewer than 8 bytes, a zero
    // quantiser scale or a version above 2. A damaged block does not fail the frame: it ends where the damage starts.
    bool decode(std::span<const std::uint8_t> bitstream, int width, int height, std::uint8_t* rgb) const;
};

}  // namespace sh::fmv
