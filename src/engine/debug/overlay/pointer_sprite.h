// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The picture of the console's mouse pointer. It is the game's own 32x32 arrow, taken from the video memory
// the running game uploaded it to (nothing of the disc is kept in the source); until that upload has happened
// the engine draws a plain arrow of its own. Plain memory in, plain memory out.

#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace sh::debug {

inline constexpr int kPointerSize = 32;
using PointerImage = std::array<std::uint8_t, kPointerSize * kPointerSize * 4>;  // RGBA rows, top first

// Where the game keeps the arrow in video memory (16-bit words): the 4-bit texels and their 16 colours.
inline constexpr int kPointerTexelsX = 768, kPointerTexelsY = 64, kPointerTexelWords = 8;
inline constexpr int kPointerColoursX = 192, kPointerColoursY = 0;

// Decodes 32 rows of 8 words (four 4-bit texels a word, the lowest nibble leftmost) through 16 colours of
// 5 bits a channel; the colour word 0 is transparent.
PointerImage decode_pointer(std::span<const std::uint16_t, kPointerTexelWords * kPointerSize> texels,
                            std::span<const std::uint16_t, 16> colours);

// Whether a decoded image is the game's arrow (video memory holds other pictures there before the game's
// interface art is uploaded).
bool is_game_pointer(const PointerImage& image);

// The engine's own arrow, tip in the top-left corner.
PointerImage plain_pointer();

}  // namespace sh::debug
