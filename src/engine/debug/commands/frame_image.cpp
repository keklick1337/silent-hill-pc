// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/commands/frame_image.h"

#include "formats/png/sh_png.h"

#include <cstdio>
#include <cstring>

namespace sh::debug {

void FrameImage::make_opaque() {
    for (std::size_t alpha = 3; alpha < rgba.size(); alpha += 4) rgba[alpha] = 255;
}

FrameImage place_in_window(const FrameImage& frame, int window_width, int window_height, int left, int top,
                           int width, int height) {
    FrameImage out;
    if (frame.empty() || window_width <= 0 || window_height <= 0 || width <= 0 || height <= 0) return out;
    out.width = window_width;
    out.height = window_height;
    out.rgba.assign(static_cast<std::size_t>(window_width) * window_height * 4, 0);
    out.make_opaque();
    for (int y = top < 0 ? 0 : top; y < top + height && y < window_height; ++y) {
        const std::size_t source_row = static_cast<std::size_t>(static_cast<long long>(y - top) * frame.height / height);
        for (int x = left < 0 ? 0 : left; x < left + width && x < window_width; ++x) {
            const std::size_t source_column =
                static_cast<std::size_t>(static_cast<long long>(x - left) * frame.width / width);
            std::memcpy(&out.rgba[(static_cast<std::size_t>(y) * window_width + x) * 4],
                        &frame.rgba[(source_row * frame.width + source_column) * 4], 4);
        }
    }
    return out;
}

Result<std::size_t> write_png(const FrameImage& image, const std::string& path) {
    if (image.empty()) return Result<std::size_t>::fail("png encode failed");
    sh::png::Image picture;
    picture.width = static_cast<std::uint32_t>(image.width);
    picture.height = static_cast<std::uint32_t>(image.height);
    picture.color_type = sh::png::ColorType::Rgba;
    picture.pixels.assign(image.rgba.begin(),
                          image.rgba.begin() + static_cast<std::ptrdiff_t>(image.width) * image.height * 4);
    const auto encoded = sh::png::encode(picture);
    if (!encoded || encoded.value().empty()) return Result<std::size_t>::fail("png encode failed");

    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return Result<std::size_t>::fail("cannot write " + path);
    const std::size_t size = encoded.value().size();
    const std::size_t written = std::fwrite(encoded.value().data(), 1, size, file);
    std::fclose(file);
    if (written != size) return Result<std::size_t>::fail("short write to " + path);
    return Result<std::size_t>::ok(size);
}

}  // namespace sh::debug
