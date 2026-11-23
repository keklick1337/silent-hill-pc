#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace sh {

// Read-only random access to bytes: a plain file, or a range of a disc image.
// read_at may be called from several threads at once.
class ByteSource {
public:
    virtual ~ByteSource() = default;

    virtual std::uint64_t size() const = 0;

    // False when [offset, offset + count) is outside the source or the read comes back short.
    virtual bool read_at(std::uint64_t offset, void* dst, std::size_t count) const = 0;
};

// Null when the path is not a readable regular file.
std::unique_ptr<ByteSource> open_file_source(const std::filesystem::path& path);

}  // namespace sh
