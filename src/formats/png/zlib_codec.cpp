#include "formats/png/zlib_codec.h"

#include <string>

#include "miniz/include/miniz.h"

namespace sh::png::detail {

Result<std::vector<std::uint8_t>> zlib_compress(std::span<const std::uint8_t> data, int level) {
    mz_ulong capacity = mz_compressBound(static_cast<mz_ulong>(data.size()));
    std::vector<std::uint8_t> out(capacity);
    const int status = mz_compress2(out.data(), &capacity, data.data(), static_cast<mz_ulong>(data.size()), level);
    if (status != MZ_OK) {
        return Result<std::vector<std::uint8_t>>::fail("zlib: compression failed (" + std::to_string(status) + ")");
    }
    out.resize(capacity);
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

Result<std::vector<std::uint8_t>> zlib_decompress(std::span<const std::uint8_t> stream, std::size_t expected_size) {
    std::vector<std::uint8_t> out(expected_size);
    mz_ulong produced = static_cast<mz_ulong>(expected_size);
    const int status = mz_uncompress(out.data(), &produced, stream.data(), static_cast<mz_ulong>(stream.size()));
    if (status == MZ_BUF_ERROR) {
        // Either the stream is cut short or it holds more than the image needs.
        return Result<std::vector<std::uint8_t>>::fail("zlib: the stream is truncated or longer than the image");
    }
    if (status != MZ_OK) {
        return Result<std::vector<std::uint8_t>>::fail("zlib: corrupt stream or Adler-32 mismatch");
    }
    if (produced != expected_size) {
        return Result<std::vector<std::uint8_t>>::fail("zlib: the stream is shorter than the image");
    }
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

}  // namespace sh::png::detail
