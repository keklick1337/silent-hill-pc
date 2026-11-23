#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

namespace sh::disc {

// A PS1 disc in raw MODE2/2352 form, opened from a .bin file or from the .cue sheet that names one.
// Only the first data track is read; the Silent Hill disc has a single track.
// Reads may be called from several threads at once.
class Image {
public:
    // Null when the file is missing, is not a .bin/.cue this reader supports, or its size is not a
    // multiple of 2352.
    static std::unique_ptr<Image> open(const std::filesystem::path& path);
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

}  // namespace sh::disc
