#pragma once

#include "formats/common/status.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// zlib streams (RFC 1950) through miniz. A separate translation unit, because miniz.h defines macros named
// crc32, adler32 and compress.
namespace sh::png::detail {

// Deflate (level 0..10, miniz; 9 is the best of zlib), with the zlib header and the Adler-32 trailer.
Result<std::vector<std::uint8_t>> zlib_compress(std::span<const std::uint8_t> data, int level = 9);

// The stream must inflate to exactly expected_size bytes; the Adler-32 is checked.
Result<std::vector<std::uint8_t>> zlib_decompress(std::span<const std::uint8_t> stream, std::size_t expected_size);

}  // namespace sh::png::detail
