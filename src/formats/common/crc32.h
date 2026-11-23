#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sh {

// CRC-32 as zlib.crc32 computes it (IEEE 802.3, reflected). Header-only, so sh_common needs no new source file.
inline std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    static constexpr std::array<std::uint32_t, 256> kTable = [] {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        return table;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = kTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

}  // namespace sh
