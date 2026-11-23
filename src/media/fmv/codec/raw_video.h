// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The picture codecs an AVI replacement movie may use without FFmpeg, and the uncompressed ones turned into
// packed RGB24 (top row first). Motion JPEG frames are decoded by the player with libjpeg.

#include "media/fmv/codec/avi_file.h"

#include <cstdint>
#include <span>
#include <vector>

namespace sh::fmv {

enum class AviCodec {
    MotionJpeg,
    Rgb,   // uncompressed DIB, 24 or 32 bits
    Yuy2,  // packed 4:2:2, Y U Y V
    Uyvy,  // packed 4:2:2, U Y V Y
    I420,  // planar 4:2:0: Y, U, V
    Yv12,  // planar 4:2:0: Y, V, U
    Nv12,  // 4:2:0: Y, then U and V interleaved
    Unsupported,
};

AviCodec avi_codec(const AviVideoFormat& format);

// One uncompressed frame to RGB24: `rgb` becomes width * |height| * 3 bytes. YUV is taken as BT.601 limited
// range. False when the codec is not an uncompressed one or the chunk is too short for the picture.
bool unpack_raw_frame(AviCodec codec, const AviVideoFormat& format, std::span<const std::uint8_t> chunk,
                      std::vector<std::uint8_t>& rgb);

}  // namespace sh::fmv
