// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#pragma once

// The footer the game's records carry inside GAME and OPTS: the XOR of every byte
// of the record up to the footer, stored twice, and the magic 0xDCDC. The bytes of the footer itself are zero while
// the checksum is taken. Part of the save format: unchanged since the PS1 card files.

#include <cstddef>
#include <cstdint>
#include <span>

namespace sh::save {

inline constexpr std::uint16_t kFooterMagic = 0xDCDC;
inline constexpr std::size_t kFooterSize = 4;  // checksum, checksum, magic (little endian)

// XOR of all `bytes`.
constexpr std::uint8_t xor_checksum(std::span<const std::uint8_t> bytes) {
    std::uint8_t sum = 0;
    for (std::uint8_t b : bytes) {
        sum ^= b;
    }
    return sum;
}

// `record` is the whole record with its footer as the last four bytes. Stores the magic and the checksum of the
// record taken with the checksum bytes zero.
inline void seal(std::span<std::uint8_t> record) {
    std::uint8_t* footer = record.data() + record.size() - kFooterSize;
    footer[0] = footer[1] = 0;
    footer[2] = static_cast<std::uint8_t>(kFooterMagic & 0xFF);
    footer[3] = static_cast<std::uint8_t>(kFooterMagic >> 8);
    footer[0] = footer[1] = xor_checksum(record);
}

// The stored checksum is the XOR of the record without it: taking the XOR over the record with the two equal
// checksum bytes in it gives the same value, as the card driver did.
inline bool verify(std::span<const std::uint8_t> record) {
    if (record.size() < kFooterSize) {
        return false;
    }
    const std::uint8_t* footer = record.data() + record.size() - kFooterSize;
    const std::uint16_t magic = static_cast<std::uint16_t>(footer[2] | (footer[3] << 8));
    return footer[0] == xor_checksum(record) && magic == kFooterMagic;
}

}  // namespace sh::save
