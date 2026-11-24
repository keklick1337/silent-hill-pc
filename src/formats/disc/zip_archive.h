#pragma once

#include "formats/common/byte_source.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::disc {

// One file inside a .zip archive.
struct ZipMember {
    std::string name;        // path inside the archive, '/'-separated
    std::uint64_t size = 0;  // uncompressed size
    bool directory = false;
    bool readable = false;  // stored or deflated, not encrypted
};

// Read-only access to the members of a .zip archive (zip64 included), so a disc image can stay zipped: nothing is
// unpacked to disk. A stored member is read in place. A deflated one is read through restart points (the inflater's
// state and its 32 KiB window) taken every few MiB as the stream is first read through, plus a small cache of
// decoded spans: random reads after the first pass cost at most one span of inflation.
class ZipArchive {
public:
    // Null when the file cannot be read or is not a zip archive.
    static std::unique_ptr<ZipArchive> open(const std::filesystem::path& path);
    // The same over any source; the archive keeps it.
    static std::unique_ptr<ZipArchive> open(std::shared_ptr<const ByteSource> file);
    ~ZipArchive();
    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    const std::vector<ZipMember>& members() const;

    // The member whose name equals `name`, ignoring ASCII case.
    std::optional<std::size_t> find(std::string_view name) const;

    // Null for a member that is not readable or whose local header is damaged. The source keeps the archive file
    // open by itself and may outlive this object; reads may come from several threads at once.
    std::unique_ptr<ByteSource> open_member(std::size_t index) const;

private:
    struct Impl;
    explicit ZipArchive(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// True for a path ending in ".zip" (any case).
bool is_zip_path(const std::filesystem::path& path);

}  // namespace sh::disc
