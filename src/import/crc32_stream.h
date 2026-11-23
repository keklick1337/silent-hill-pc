#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace sh::import {

// Incremental CRC-32 with the same result as sh::crc32 (formats/common/crc32.h), which only takes a whole buffer.
// Files of 50 MB are checksummed chunk by chunk here instead of being loaded whole.
class Crc32Stream {
public:
    void update(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            crc_ = table()[(crc_ ^ p[i]) & 0xFF] ^ (crc_ >> 8);
        }
    }

    std::uint32_t value() const { return ~crc_; }

private:
    static const std::array<std::uint32_t, 256>& table() {
        static const std::array<std::uint32_t, 256> kTable = [] {
            std::array<std::uint32_t, 256> t{};
            for (std::uint32_t i = 0; i < 256; ++i) {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                }
                t[i] = c;
            }
            return t;
        }();
        return kTable;
    }

    std::uint32_t crc_ = 0xFFFFFFFFu;
};

}  // namespace sh::import
