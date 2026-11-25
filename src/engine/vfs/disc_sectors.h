#pragma once

#include "engine/vfs/vfs.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::vfs {

enum class SectorKind : std::uint8_t {
    Data,  // 2048 bytes: Form 1 user data, raw sector bytes 24..2071
    Xa,    // 2336 bytes: raw sector bytes 16..2351 (subheader, payload, EDC)
};

constexpr std::size_t kDataSectorBytes = 2048;
constexpr std::size_t kXaSectorBytes = 2336;

inline std::size_t sector_bytes(SectorKind kind) {
    return kind == SectorKind::Xa ? kXaSectorBytes : kDataSectorBytes;
}

// A run of disc sectors that holds one file of the game.
struct SectorRun {
    std::uint32_t lba = 0;
    std::uint32_t sectors = 0;
    std::string path;       // canonical game path
    std::uint64_t size = 0; // logical size
    bool hill = false;      // HILL. sectors are stored as 2336 bytes each, SILENT. sectors as 2048
};

// Reads by LBA through the VFS: the LBA is resolved to a file of the game and an offset in it, and the bytes
// come from the layer that answers for that path, so a mod that replaces the file also replaces its sectors.
//
// The LBA map is the game's file table (gamedata/tables/filetable.json), with the logical sizes of
// gamedata/index.tsv. SILENT. sectors give Data reads (the bytes past a file's logical size in its last sector come
// from the layer's open_stored()); HILL. sectors are
// stored whole as 2336 bytes, so they give both Xa reads and Data reads (bytes 8..2055). Sectors outside every run
// (the ISO 9660 area, gaps) and Xa reads of SILENT. sectors are not in gamedata/: they read as zeros and false.
class DiscSectors {
public:
    // Null with `error` set when the map cannot be built.
    static std::unique_ptr<DiscSectors> open(const Vfs& vfs, const std::filesystem::path& gamedata_dir,
                                             std::string& error);

    std::uint32_t sector_count() const;
    const std::vector<SectorRun>& runs() const { return runs_; }

    // Fills sector_bytes(kind) bytes. Thread safe.
    bool read(std::uint32_t lba, SectorKind kind, std::uint8_t* dst) const;

    // The run of a game path ("\\MOVIE\\INTRO.STR;1" and "movie/intro.str" both work), or null.
    const SectorRun* find(std::string_view path) const;

    // The run that holds `lba`, or null.
    const SectorRun* run_at(std::uint32_t lba) const;

private:
    DiscSectors(const Vfs& vfs, std::vector<SectorRun> runs);

    std::shared_ptr<const ByteSource> source_for(const SectorRun& run) const;

    const Vfs& vfs_;
    std::vector<SectorRun> runs_;  // sorted by lba, not overlapping

    struct Cached {
        const SectorRun* run = nullptr;
        std::shared_ptr<const ByteSource> source;
    };
    mutable std::mutex mutex_;
    mutable std::vector<Cached> cache_;  // a few open files, most recent last
};

// Bytes of one sector straight from the image, at the offset the kind names. For the check mode that compares
// every runtime read with the image.
bool read_image_sector(const disc::Image& image, std::uint32_t lba, SectorKind kind, std::uint8_t* dst);

}  // namespace sh::vfs
