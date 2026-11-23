#pragma once

#include "formats/common/byte_source.h"
#include "formats/disc/disc_image.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::iso9660 {

struct FileInfo {
    std::string name;    // as recorded on the disc, without the ";1" version suffix
    std::uint32_t lba;   // first sector of the file data
    std::uint32_t size;  // bytes
    bool directory;
};

// ISO 9660 volume of a disc image. Only lookup, listing and reading are provided.
class Volume {
public:
    // Null when sector 16 is not a primary volume descriptor with 2048-byte blocks.
    static std::optional<Volume> open(std::shared_ptr<const disc::Image> image);

    // "SLUS_007.07", "SILENT.", or "" for the root. '/' separates folders; matching is case-insensitive.
    std::optional<FileInfo> find(std::string_view path) const;

    // Entries of a folder, without "." and "..". Empty when dir is not a folder.
    std::vector<FileInfo> list(std::string_view dir) const;

    // Null for folders. The returned source keeps the image alive.
    std::unique_ptr<ByteSource> open_file(const FileInfo& file) const;

private:
    Volume(std::shared_ptr<const disc::Image> image, FileInfo root);

    std::shared_ptr<const disc::Image> image_;
    FileInfo root_;
};

}  // namespace sh::iso9660
