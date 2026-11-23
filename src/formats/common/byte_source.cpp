#include "formats/common/byte_source.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <system_error>
#include <utility>

namespace sh {

namespace {

// All reads share one stream, so the mutex is what makes read_at safe from several threads.
class FileSource final : public ByteSource {
public:
    FileSource(std::ifstream in, std::uint64_t size) : in_(std::move(in)), size_(size) {}

    std::uint64_t size() const override { return size_; }

    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > size_ || count > size_ - offset) {
            return false;
        }
        if (count == 0) {
            return true;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        in_.clear();
        in_.seekg(static_cast<std::streamoff>(offset));
        in_.read(static_cast<char*>(dst), static_cast<std::streamsize>(count));
        return static_cast<std::size_t>(in_.gcount()) == count;
    }

private:
    mutable std::mutex mutex_;
    mutable std::ifstream in_;
    std::uint64_t size_;
};

}  // namespace

std::unique_ptr<ByteSource> open_file_source(const std::filesystem::path& path) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return nullptr;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return nullptr;
    }
    return std::make_unique<FileSource>(std::move(in), static_cast<std::uint64_t>(size));
}

}  // namespace sh
