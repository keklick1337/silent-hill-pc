// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// BC7 pictures in a DDS container, the compressed form a mod may ship instead of a PNG (
// the section on models and textures). Only the DX10 header with a BC7 format is read: BC7 keeps a real 8-bit alpha,
// which the replacement shader's cutout needs, and has no older four-character code to guess from. The renderer has no
// compressed texture formats, so the largest level is expanded to RGBA8 here.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "formats/common/status.h"

namespace sh::formats::texture {

// A picture as rows of R, G, B, A bytes, top row first.
struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// What the container says about a BC7 picture; `blocks` points into the file that was read.
struct Bc7Picture {
    int width = 0;
    int height = 0;
    int levels = 1;  // mip levels in the file, the largest first
    bool srgb = false;
    std::span<const std::uint8_t> blocks;
};

// True for bytes that start like a DDS file, whatever is inside.
bool is_dds(std::span<const std::uint8_t> file);

// The header of a BC7 DDS. Fails with the format's name for any other DDS, so a picture the game cannot show is
// named in the log instead of vanishing.
Result<Bc7Picture> read_bc7(std::span<const std::uint8_t> file);

// The largest level as RGBA8. Fails like read_bc7, and for a file shorter than its blocks.
Result<RgbaImage> decode_bc7(std::span<const std::uint8_t> file);

}  // namespace sh::formats::texture
