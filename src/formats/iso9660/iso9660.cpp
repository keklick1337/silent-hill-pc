#include "formats/iso9660/iso9660.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sh::iso9660 {

namespace {

constexpr std::uint32_t kBlockSize = 2048;
constexpr std::uint32_t kPrimaryVolumeSector = 16;
constexpr std::size_t kBlockSizeOffset = 128;   // logical block size, 16-bit little-endian
constexpr std::size_t kRootRecordOffset = 156;  // root directory record inside the descriptor
constexpr std::uint8_t kFlagDirectory = 0x02;
constexpr std::uint8_t kMinRecordLength = 34;   // 33 bytes of fixed fields and one name byte

std::uint32_t read_le32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | std::uint32_t{p[1]} << 8 | std::uint32_t{p[2]} << 16 |
           std::uint32_t{p[3]} << 24;
}

std::uint16_t read_le16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

bool equal_ignore_case(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

// "SLUS_007.07;1" becomes "SLUS_007.07". A name without a version number is returned unchanged.
std::string strip_version(std::string name) {
    const std::size_t semicolon = name.rfind(';');
    if (semicolon != std::string::npos && semicolon + 1 < name.size() &&
        std::all_of(name.begin() + static_cast<std::ptrdiff_t>(semicolon + 1), name.end(),
                    [](char c) { return c >= '0' && c <= '9'; })) {
        name.resize(semicolon);
    }
    return name;
}

// Parses one directory record. Returns nullopt for "." and "..", which are not entries.
std::optional<FileInfo> parse_record(const std::uint8_t* record) {
    const std::uint8_t name_length = record[32];
    if (name_length == 1 && (record[33] == 0 || record[33] == 1)) {
        return std::nullopt;
    }
    FileInfo info;
    info.name = strip_version(std::string(reinterpret_cast<const char*>(record + 33), name_length));
    info.lba = read_le32(record + 2);
    info.size = read_le32(record + 10);
    info.directory = (record[25] & kFlagDirectory) != 0;
    return info;
}

// Calls visit for each entry of a folder, in disc order, until visit returns false. Returns false
// when a sector cannot be read or a record is malformed. Records never cross a sector boundary;
// the space left at the end of a sector is zero-filled.
template <typename Visit>
bool walk_folder(const disc::Image& image, const FileInfo& folder, Visit&& visit) {
    std::uint8_t sector[kBlockSize];
    for (std::uint64_t start = 0; start < folder.size; start += kBlockSize) {
        const auto lba = static_cast<std::uint32_t>(folder.lba + start / kBlockSize);
        if (!image.read_user_data(lba, sector)) {
            return false;
        }
        const std::uint64_t limit = std::min<std::uint64_t>(kBlockSize, folder.size - start);
        std::uint32_t pos = 0;
        while (pos < limit && sector[pos] != 0) {
            const std::uint8_t length = sector[pos];
            if (length < kMinRecordLength || pos + length > kBlockSize ||
                33u + sector[pos + 32] > length) {
                return false;
            }
            if (std::optional<FileInfo> entry = parse_record(sector + pos)) {
                if (!visit(*entry)) {
                    return true;
                }
            }
            pos += length;
        }
    }
    return true;
}

// Follows '/'-separated folder names from the root. Empty parts are skipped, so "" is the root.
std::optional<FileInfo> resolve(const disc::Image& image, const FileInfo& root, std::string_view path) {
    FileInfo current = root;
    while (!path.empty()) {
        const std::size_t slash = path.find('/');
        const std::string_view part = path.substr(0, slash);
        path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
        if (part.empty()) {
            continue;
        }
        if (!current.directory) {
            return std::nullopt;
        }
        std::optional<FileInfo> found;
        const bool complete = walk_folder(image, current, [&](const FileInfo& entry) {
            if (!equal_ignore_case(entry.name, part)) {
                return true;
            }
            found = entry;
            return false;
        });
        if (!complete || !found) {
            return std::nullopt;
        }
        current = std::move(*found);
    }
    return current;
}

// Reads a file straight from the image. Full blocks are copied directly into the caller's buffer.
class FileSource final : public ByteSource {
public:
    FileSource(std::shared_ptr<const disc::Image> image, std::uint32_t lba, std::uint32_t size)
        : image_(std::move(image)), lba_(lba), size_(size) {}

    std::uint64_t size() const override { return size_; }

    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > size_ || count > size_ - offset) {
            return false;
        }
        auto* out = static_cast<std::uint8_t*>(dst);
        while (count > 0) {
            const auto block = lba_ + static_cast<std::uint32_t>(offset / kBlockSize);
            const auto within = static_cast<std::size_t>(offset % kBlockSize);
            const std::size_t n = std::min<std::size_t>(count, kBlockSize - within);
            if (within == 0 && n == kBlockSize) {
                if (!image_->read_user_data(block, out)) {
                    return false;
                }
            } else {
                std::uint8_t sector[kBlockSize];
                if (!image_->read_user_data(block, sector)) {
                    return false;
                }
                std::memcpy(out, sector + within, n);
            }
            out += n;
            offset += n;
            count -= n;
        }
        return true;
    }

private:
    std::shared_ptr<const disc::Image> image_;
    std::uint32_t lba_;
    std::uint32_t size_;
};

}  // namespace

Volume::Volume(std::shared_ptr<const disc::Image> image, FileInfo root)
    : image_(std::move(image)), root_(std::move(root)) {}

std::optional<Volume> Volume::open(std::shared_ptr<const disc::Image> image) {
    if (!image) {
        return std::nullopt;
    }
    std::uint8_t pvd[kBlockSize];
    if (!image->read_user_data(kPrimaryVolumeSector, pvd)) {
        return std::nullopt;
    }
    // Type 1 is the primary volume descriptor.
    if (pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0) {
        return std::nullopt;
    }
    if (read_le16(pvd + kBlockSizeOffset) != kBlockSize) {
        return std::nullopt;
    }
    const std::uint8_t* root = pvd + kRootRecordOffset;
    if ((root[25] & kFlagDirectory) == 0) {
        return std::nullopt;
    }
    return Volume(std::move(image), FileInfo{"", read_le32(root + 2), read_le32(root + 10), true});
}

std::optional<FileInfo> Volume::find(std::string_view path) const {
    return resolve(*image_, root_, path);
}

std::vector<FileInfo> Volume::list(std::string_view dir) const {
    std::vector<FileInfo> entries;
    const std::optional<FileInfo> folder = resolve(*image_, root_, dir);
    if (!folder || !folder->directory) {
        return entries;
    }
    const bool complete = walk_folder(*image_, *folder, [&](const FileInfo& entry) {
        entries.push_back(entry);
        return true;
    });
    if (!complete) {
        entries.clear();
    }
    return entries;
}

std::unique_ptr<ByteSource> Volume::open_file(const FileInfo& file) const {
    if (file.directory) {
        return nullptr;
    }
    return std::make_unique<FileSource>(image_, file.lba, file.size);
}

}  // namespace sh::iso9660
