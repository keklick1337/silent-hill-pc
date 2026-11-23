#pragma once

// Integer math of the world queries. The results must equal the original game's to the last bit (replay), so the
// square root is the table-driven one the game uses, not std::sqrt.

#include <array>
#include <bit>
#include <cstdint>

namespace sh::world {

namespace detail {

constexpr std::uint32_t integer_sqrt(std::uint64_t value) {
    std::uint64_t root = 0;
    std::uint64_t bit = std::uint64_t{1} << 62;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return static_cast<std::uint32_t>(root);
}

// sqrt(m / 64) in Q.12, rounded down, for the normalised mantissas m = 64..255.
constexpr std::array<std::int32_t, 192> make_sqrt_mantissas() {
    std::array<std::int32_t, 192> table{};
    for (std::uint32_t m = 64; m < 256; ++m) {
        table[m - 64] = static_cast<std::int32_t>(integer_sqrt(std::uint64_t{262144} * m));
    }
    return table;
}

inline constexpr std::array<std::int32_t, 192> kSqrtMantissas = make_sqrt_mantissas();

}  // namespace detail

// Square root of a non-negative integer, as the game computes it: the value is normalised to an 8-bit mantissa with
// an even shift, the mantissa's root comes from a table and is shifted back. Exact for small squares, slightly low
// for large ones. Negative input gives 0 (the game never passes one).
constexpr std::int32_t game_sqrt(std::int32_t value) {
    if (value <= 0) {
        return 0;
    }
    const int zeros = std::countl_zero(static_cast<std::uint32_t>(value)) & ~1;
    const std::int32_t mantissa = zeros < 24 ? value >> (24 - zeros) : value << (zeros - 24);
    return (detail::kSqrtMantissas[static_cast<std::size_t>(mantissa - 64)] << ((31 - zeros) >> 1)) >> 12;
}

// 32-bit products and sums wrap like the game's arithmetic instead of being undefined.
constexpr std::int32_t wrap_mul(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

constexpr std::int32_t wrap_add(std::int32_t a, std::int32_t b) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

// Length of a 2D vector with the game's square root.
constexpr std::int32_t planar_length(std::int32_t x, std::int32_t z) {
    return game_sqrt(wrap_add(wrap_mul(x, x), wrap_mul(z, z)));
}

// (a * b) >> 12 in 32 bits, the game's fixed-point product.
constexpr std::int32_t mul_q12(std::int32_t a, std::int32_t b) {
    return wrap_mul(a, b) >> 12;
}

// Coordinates of (dx, dz) in the frame of a wall edge with unit direction (dir_x, dir_z), Q.12: `along` the edge and
// `across` it (non-negative on the side of WallEdge::floor_left). Both inputs and both outputs are 16-bit like the game's vector unit;
// the perpendicular axis of that unit's matrix is (-dir_z - carry, dir_x), where carry is 1 when dir_x is not zero:
// the matrix row is the 32-bit negation of the packed direction, and that borrow is part of the original result.
struct EdgeFrame {
    std::int16_t along = 0;
    std::int16_t across = 0;
};

constexpr EdgeFrame to_edge_frame(std::int16_t dir_x, std::int16_t dir_z, std::int32_t dx, std::int32_t dz) {
    const std::int64_t x = static_cast<std::int16_t>(dx);
    const std::int64_t z = static_cast<std::int16_t>(dz);
    const std::uint32_t packed =
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(dir_x)) |
        (static_cast<std::uint32_t>(static_cast<std::uint16_t>(dir_z)) << 16);
    const auto perpendicular_x = static_cast<std::int16_t>((0u - packed) >> 16);
    EdgeFrame frame;
    frame.along = static_cast<std::int16_t>(static_cast<std::int32_t>((dir_x * x + dir_z * z) >> 12));
    frame.across = static_cast<std::int16_t>(static_cast<std::int32_t>((perpendicular_x * x + dir_x * z) >> 12));
    return frame;
}

}  // namespace sh::world
