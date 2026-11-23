// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/debug/overlay/pointer_sprite.h"

namespace sh::debug {

namespace {

// FNV-1a over the arrow's RGBA bytes as the NTSC-U disc's interface art decodes.
constexpr std::uint64_t kGamePointerHash = 0x8B8AC9BD524025C0ULL;

std::uint64_t fnv1a(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

// The plain arrow's outline: a triangle from the tip down the left edge.
bool inside_plain_arrow(int x, int y) {
    return x >= 0 && y >= x && y * 13 <= 18 * 13 - x * 5;
}

}  // namespace

PointerImage decode_pointer(std::span<const std::uint16_t, kPointerTexelWords * kPointerSize> texels,
                            std::span<const std::uint16_t, 16> colours) {
    PointerImage image{};
    for (int y = 0; y < kPointerSize; ++y) {
        for (int x = 0; x < kPointerSize; ++x) {
            const std::uint16_t word = texels[static_cast<std::size_t>(y * kPointerTexelWords + x / 4)];
            const std::uint16_t colour = colours[(word >> ((x % 4) * 4)) & 0xFu];
            std::uint8_t* out = image.data() + static_cast<std::size_t>(y * kPointerSize + x) * 4;
            out[0] = static_cast<std::uint8_t>((colour & 0x1Fu) << 3);
            out[1] = static_cast<std::uint8_t>(((colour >> 5) & 0x1Fu) << 3);
            out[2] = static_cast<std::uint8_t>(((colour >> 10) & 0x1Fu) << 3);
            out[3] = colour == 0 ? 0 : 255;
        }
    }
    return image;
}

bool is_game_pointer(const PointerImage& image) { return fnv1a(image) == kGamePointerHash; }

PointerImage plain_pointer() {
    PointerImage image{};
    for (int y = 0; y < kPointerSize; ++y) {
        for (int x = 0; x < kPointerSize; ++x) {
            if (!inside_plain_arrow(x, y)) continue;
            const bool rim = !inside_plain_arrow(x - 1, y) || !inside_plain_arrow(x + 1, y) ||
                             !inside_plain_arrow(x, y - 1) || !inside_plain_arrow(x, y + 1);
            const std::uint8_t shade = rim ? 24 : 232;
            std::uint8_t* out = image.data() + static_cast<std::size_t>(y * kPointerSize + x) * 4;
            out[0] = out[1] = out[2] = shade;
            out[3] = 255;
        }
    }
    return image;
}

}  // namespace sh::debug
