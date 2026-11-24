#pragma once

#include "formats/common/byte_source.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace sh::disc {

// A PS1 disc in raw MODE2/2352 form, opened from a .bin file, from the .cue sheet that names one, or from a .zip
// archive holding either (open_zip_image).
// Only the first data track is read; the Silent Hill disc has a single track.
// Reads may be called from several threads at once.
class Image {
public:
    // Null when the file is missing, is not a .bin/.cue/.zip this reader supports, or its size is not a
    // multiple of 2352.
    static std::unique_ptr<Image> open(const std::filesystem::path& path);
    // The same over the sectors of any source (a member of a zip archive).
    static std::unique_ptr<Image> open(std::unique_ptr<ByteSource> source);
    ~Image();

    std::uint32_t sector_count() const;

    // False when sector 0 does not start with the CD sync pattern. Opening still succeeds: the file
    // is then probably not raw MODE2/2352 at this offset.
    bool sync_ok() const;

    // Copies the 2048 bytes of Form 1 user data (raw bytes 24..2071) of sector lba into dst.
    bool read_user_data(std::uint32_t lba, void* dst) const;

    // Copies all 2352 raw bytes of sector lba (sync, header, data, EDC and ECC) into dst.
    bool read_raw(std::uint32_t lba, void* dst) const;

private:
    struct Impl;
    explicit Image(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

// The disc image inside a .zip archive. A cue sheet (the first by name that parses) naming a member of the archive
// wins; otherwise the first .bin, then .iso, then .img by name. The member is read in place (ZipArchive), never
// unpacked to disk.
struct ZipImage {
    enum class Status {
        Ok,
        NotZip,            // unreadable, or not a zip archive
        NoImage,           // no .cue, .bin, .iso or .img member that can be read
        CueNoFile,         // a cue sheet without a usable FILE / TRACK MODE2/2352 line, and nothing else to take
        CueTargetMissing,  // the cue names a file the archive does not have, and nothing else to take
    };
    Status status = Status::NotZip;
    std::string cue;                     // member name of the cue sheet that named the image, if one did
    std::string member;                  // member name of the image itself
    std::string cue_target;              // the FILE name of the cue for CueTargetMissing
    std::uint64_t size = 0;              // uncompressed size of `member`
    std::unique_ptr<ByteSource> source;  // the member's bytes when Ok

    bool ok() const { return status == Status::Ok; }
};

ZipImage open_zip_image(const std::filesystem::path& zip);

}  // namespace sh::disc
