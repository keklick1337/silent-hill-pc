// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// A picture file of a mod as RGBA8, whatever it is: a PNG, a BC7 DDS, or a TIM (a mod may put a larger TIM at the
// disc path of the one it replaces). The kind is told from the first bytes, not from the file's name.

#pragma once

#include <cstdint>
#include <span>

#include "formats/common/status.h"
#include "formats/texture/bc7_dds.h"

namespace sh::formats::texture {

// The longest side of a picture the game takes.
inline constexpr int kPictureSideMax = 8192;

// A TIM seen through the first row of its palette. A palette word of 0 is a transparent texel (alpha 0), every
// other texel is opaque; 5-bit channels widen by repeating their top bits. Direct-colour TIMs (16 and 24 bits)
// follow the same rule per texel, 24-bit ones being opaque throughout.
Result<RgbaImage> decode_tim(std::span<const std::uint8_t> file);

// Any of the three kinds. Fails with the reason for a file that is none of them, is damaged, or is larger than
// kPictureSideMax on a side.
Result<RgbaImage> decode_picture(std::span<const std::uint8_t> file);

}  // namespace sh::formats::texture
