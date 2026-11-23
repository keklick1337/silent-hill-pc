#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace sh::import {

// SHA-256 (FIPS 180-4). Incremental, so a whole disc image can be hashed without holding it in memory.
class Sha256 {
public:
    using Digest = std::array<std::uint8_t, 32>;

    Sha256();

    void update(const void* data, std::size_t size);

    // Finishes the hash. The object must not be updated afterwards.
    Digest finish();

private:
    void process_block(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_;
    std::size_t buffered_ = 0;
    std::uint64_t total_bytes_ = 0;
};

// Lower-case hexadecimal, 64 characters.
std::string to_hex(const Sha256::Digest& digest);

std::string sha256_hex(const void* data, std::size_t size);

}  // namespace sh::import
