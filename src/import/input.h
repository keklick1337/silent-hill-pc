#pragma once

// Internal to sh_import: opening and recognising the disc image. Not part of the module contract (import.h).

#include "formats/archive/sh_archive.h"
#include "formats/common/byte_source.h"
#include "formats/common/status.h"
#include "formats/disc/disc_image.h"
#include "formats/iso9660/iso9660.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sh::import {

inline constexpr const char* kSupportedImageSha256 = "cf975c962d8ecd7b57ed43127fb5ce41a693c713ae4917c14e79dabde5250794";
inline constexpr const char* kSupportedExeSha256 = "e73859ccd2e8000d259c6fe640bb8a6d55fed6044f67fbf071e3d86c0f202398";
inline constexpr const char* kGameId = "SLUS-007.07";

// Opens .cue, .bin (MODE2/2352), a raw .iso (2352 bytes per sector) or a .zip holding one of them (read in place,
// disc::open_zip_image). Refuses a 2048-byte .iso and single files
// with a message that says what is needed instead. A raw .iso is opened through `alias`, a link to it that ends in
// ".bin" (sh_disc recognises the container by extension); the caller removes `alias` afterwards.
Result<std::shared_ptr<const disc::Image>> open_image(const std::filesystem::path& input,
                                                      const std::filesystem::path& alias);

// The parts of the disc the import reads, after the version checks.
struct GameDisc {
    std::shared_ptr<const disc::Image> image;
    std::shared_ptr<const ByteSource> system_cnf;
    std::shared_ptr<const ByteSource> slus;
    std::shared_ptr<const ByteSource> silent;  // SILENT. as an ISO file: 2048 bytes per sector
    std::shared_ptr<const ByteSource> hill;    // HILL. payload: 2336 bytes per sector
    archive::Layout silent_layout{};
    archive::Layout hill_layout{};
    std::uint32_t system_cnf_lba = 0;
    std::uint32_t slus_lba = 0;
    std::vector<archive::Entry> entries;
    std::string image_sha256;
};

// Checks ISO 9660, the SLUS_007.07 SHA-256, the file table and the SHA-256 of the whole image (a full read,
// `hash_progress` gets 0..1; `cancel`, when set, stops the read with the error "cancelled"). Anything but NTSC USA 1.1
// is refused.
Result<GameDisc> open_game(std::shared_ptr<const disc::Image> image, const std::atomic<bool>* cancel,
                           const std::function<void(double)>& hash_progress);

}  // namespace sh::import
