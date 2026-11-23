#pragma once

#include "formats/common/byte_source.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sh::disc {
class Image;
}

namespace sh::archive {

// SILENT. holds 2048 bytes of user data per sector. HILL. holds 2336 bytes per sector: raw bytes 16..2351, that is
// the XA subheader, the audio and the EDC.
enum class Region : std::uint8_t { Silent, Hill };

// A container on the disc: its first sector and the number of sectors it spans.
struct Layout {
    std::uint32_t start_lba;
    std::uint32_t sector_count;
};

// One file of the game. The id is the file's number in the game's file table.
struct Entry {
    std::uint16_t id;
    std::string name;      // "CHARA/HERO.ILM"; XA tracks have no extension: "XA/ZZ_14239"
    Region region;
    std::uint64_t offset;  // byte offset inside the container's payload stream
    std::uint32_t size;    // bytes
    std::uint32_t lba;     // absolute disc sector of the first byte of the entry
    std::uint32_t sectors; // sectors the entry occupies: ceil(size / 2048) for SILENT., size / 2336 for HILL.
};

// The file table of the game, read from SLUS_007.07. Only NTSC 1.1 (SLUS-00707) is recognised.
// It is the only index: the containers hold no names, and every entry is addressed by its LBA on the disc.
std::optional<std::vector<Entry>> read_file_table(const ByteSource& exe, Layout silent, Layout hill);

// HILL. as a byte stream: 2336 bytes per sector, from raw bytes 16..2351 of each sector. Keeps the image alive.
std::unique_ptr<ByteSource> open_hill(std::shared_ptr<const disc::Image> image, Layout hill);

// Entries of one container. `source` is SILENT. as the ISO 9660 file, or the stream from open_hill.
class Archive {
public:
    static std::optional<Archive> open(std::shared_ptr<const ByteSource> source, Region region);

    // Null when the entry belongs to the other container or does not fit in the source.
    std::optional<std::vector<std::uint8_t>> read(const Entry& entry) const;

private:
    Archive(std::shared_ptr<const ByteSource> source, Region region);

    std::shared_ptr<const ByteSource> source_;
    Region region_;
};

// The entry's `size` bytes from the payload stream of its container, raw as on disc: the sector tail after
// `size` is not included. Archive::read adds the region check.
std::optional<std::vector<std::uint8_t>> read_entry(const ByteSource& container, const Entry& entry);

}  // namespace sh::archive
