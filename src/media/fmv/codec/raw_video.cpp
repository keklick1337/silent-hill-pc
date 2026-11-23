// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/fmv/codec/raw_video.h"

#include <initializer_list>
#include <string_view>

namespace sh::fmv {

namespace {

char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

bool named(const std::array<char, 4>& fourcc, std::initializer_list<std::string_view> names) {
    for (std::string_view name : names) {
        bool same = true;
        for (std::size_t i = 0; i < 4 && same; ++i) same = upper(fourcc[i]) == name[i];
        if (same) return true;
    }
    return false;
}

std::uint8_t clip(int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

void put_yuv(int y, int u, int v, std::uint8_t* rgb) {
    const int luma = 298 * (y - 16) + 128;
    const int cb = u - 128, cr = v - 128;
    rgb[0] = clip((luma + 409 * cr) >> 8);
    rgb[1] = clip((luma - 100 * cb - 208 * cr) >> 8);
    rgb[2] = clip((luma + 516 * cb) >> 8);
}

// Rows of a DIB are padded to four bytes, hold blue first and run bottom to top unless the height is negative.
bool unpack_dib(const AviVideoFormat& format, std::size_t w, std::size_t h, std::span<const std::uint8_t> chunk,
                std::uint8_t* rgb) {
    const std::size_t pixel = static_cast<std::size_t>(format.bits_per_pixel) / 8;
    const std::size_t row_bytes = (w * pixel + 3) & ~static_cast<std::size_t>(3);
    if (h * row_bytes > chunk.size()) return false;
    for (std::size_t y = 0; y < h; ++y) {
        const std::uint8_t* src = chunk.data() + (format.height < 0 ? y : h - 1 - y) * row_bytes;
        for (std::size_t x = 0; x < w; ++x, src += pixel, rgb += 3) {
            rgb[0] = src[2];
            rgb[1] = src[1];
            rgb[2] = src[0];
        }
    }
    return true;
}

bool unpack_422(bool luma_first, std::size_t w, std::size_t h, std::span<const std::uint8_t> chunk, std::uint8_t* rgb) {
    if ((w & 1) != 0 || w * h * 2 > chunk.size()) return false;
    const std::uint8_t* src = chunk.data();
    for (std::size_t pair = 0; pair < w * h / 2; ++pair, src += 4, rgb += 6) {
        const int y0 = src[luma_first ? 0 : 1], y1 = src[luma_first ? 2 : 3];
        const int u = src[luma_first ? 1 : 0], v = src[luma_first ? 3 : 2];
        put_yuv(y0, u, v, rgb);
        put_yuv(y1, u, v, rgb + 3);
    }
    return true;
}

bool unpack_420(AviCodec codec, std::size_t w, std::size_t h, std::span<const std::uint8_t> chunk, std::uint8_t* rgb) {
    const std::size_t chroma_w = (w + 1) / 2, chroma_h = (h + 1) / 2;
    const std::size_t luma_bytes = w * h, chroma_bytes = chroma_w * chroma_h;
    if (luma_bytes + chroma_bytes * 2 > chunk.size()) return false;
    const std::uint8_t* luma = chunk.data();
    const std::uint8_t* first = luma + luma_bytes;
    // Where the U and V of chroma sample i are, and how far apart the samples of a plane sit.
    const std::size_t step = codec == AviCodec::Nv12 ? 2 : 1;
    const std::uint8_t* u = codec == AviCodec::Yv12 ? first + chroma_bytes : first;
    const std::uint8_t* v = codec == AviCodec::Yv12 ? first : codec == AviCodec::Nv12 ? first + 1 : first + chroma_bytes;
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x, rgb += 3) {
            const std::size_t i = ((y / 2) * chroma_w + x / 2) * step;
            put_yuv(luma[y * w + x], u[i], v[i], rgb);
        }
    }
    return true;
}

}  // namespace

AviCodec avi_codec(const AviVideoFormat& format) {
    const std::array<char, 4>& cc = format.fourcc;
    if (named(cc, {"MJPG", "DMB1", "JPEG", "AVI1"})) return AviCodec::MotionJpeg;
    // Uncompressed RGB has no fourcc; some writers put "DIB " there.
    if ((cc[0] == 0 || named(cc, {"DIB "})) && (format.bits_per_pixel == 24 || format.bits_per_pixel == 32)) {
        return AviCodec::Rgb;
    }
    if (named(cc, {"YUY2", "YUYV"})) return AviCodec::Yuy2;
    if (named(cc, {"UYVY"})) return AviCodec::Uyvy;
    if (named(cc, {"I420", "IYUV"})) return AviCodec::I420;
    if (named(cc, {"YV12"})) return AviCodec::Yv12;
    if (named(cc, {"NV12"})) return AviCodec::Nv12;
    return AviCodec::Unsupported;
}

bool unpack_raw_frame(AviCodec codec, const AviVideoFormat& format, std::span<const std::uint8_t> chunk,
                      std::vector<std::uint8_t>& rgb) {
    if (format.width <= 0 || format.height == 0 || format.height == INT32_MIN) return false;
    const std::size_t w = static_cast<std::size_t>(format.width);
    const std::size_t h = static_cast<std::size_t>(format.height < 0 ? -format.height : format.height);
    switch (codec) {
        case AviCodec::Rgb:
        case AviCodec::Yuy2:
        case AviCodec::Uyvy:
        case AviCodec::I420:
        case AviCodec::Yv12:
        case AviCodec::Nv12: break;
        default: return false;
    }
    rgb.resize(w * h * 3);
    switch (codec) {
        case AviCodec::Rgb: return unpack_dib(format, w, h, chunk, rgb.data());
        case AviCodec::Yuy2: return unpack_422(true, w, h, chunk, rgb.data());
        case AviCodec::Uyvy: return unpack_422(false, w, h, chunk, rgb.data());
        default: return unpack_420(codec, w, h, chunk, rgb.data());
    }
}

}  // namespace sh::fmv
