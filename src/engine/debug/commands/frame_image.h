// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Pictures the capture commands write (`shot`, `shotwin`, `shotseq`): an RGBA image in memory, the step that
// places a frame inside a window-sized picture, and the PNG file. No renderer here, so it runs in unit tests.

#pragma once

#include "formats/common/status.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sh::debug {

struct FrameImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // rows top first, four bytes a pixel

    bool empty() const { return width <= 0 || height <= 0 || rgba.size() < static_cast<std::size_t>(width) * height * 4; }
    // A frame's alpha is blend bookkeeping, not coverage: the window ignores it, so the picture is opaque too.
    void make_opaque();
};

// What the window shows: a black picture of the window's size with the frame stretched (nearest pixel) into
// the rectangle the present step blits it to. Empty when a size is not positive.
FrameImage place_in_window(const FrameImage& frame, int window_width, int window_height, int left, int top,
                           int width, int height);

// Encodes and writes the PNG. On success the value is the file's size in bytes; the error is one of
// "png encode failed", "cannot write <path>", "short write to <path>".
Result<std::size_t> write_png(const FrameImage& image, const std::string& path);

}  // namespace sh::debug
