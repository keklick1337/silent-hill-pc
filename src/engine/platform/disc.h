// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The one sector reader of the game. The XA voice players, the FMV demuxer, the disc-text
// readers and the file queue take sectors by LBA through sh::disc::read_sector. The engine installs a source that
// resolves an LBA to a file of gamedata/ (or a mod) through the VFS; without one the reads go to the raw disc image
// named by open_image.

#include <cstdint>
#include <string_view>

namespace sh::disc {

enum class SectorKind : std::uint8_t {
    Data,  // 2048 bytes: Form 1 user data, raw bytes 24..2071
    Xa,    // 2336 bytes: subheader, payload and EDC, raw bytes 16..2351
};

inline constexpr int kDataSectorBytes = 2048;
inline constexpr int kXaSectorBytes = 2336;

constexpr int sector_bytes(SectorKind kind) { return kind == SectorKind::Xa ? kXaSectorBytes : kDataSectorBytes; }

class SectorSource {
public:
    virtual ~SectorSource() = default;
    // Sectors on the disc: reads at or past this LBA are past the end.
    virtual int sector_count() = 0;
    // Fills dst (sector_bytes(kind) bytes); false when the sector cannot be produced. Called from any thread.
    virtual bool read_sector(int lba, SectorKind kind, void* dst) = 0;
};

// Installs (or with nullptr removes) the source. Not owned: it must outlive the reads.
void set_source(SectorSource* source);
bool has_source();

// Names the raw disc image (2352-byte sectors) that reads use when no source is installed.
void open_image(std::string_view path);

// True when sectors can be read: a source is installed or a disc image is named.
bool available();
int sector_count();
// Fills dst; on failure it is zero-filled and false is returned.
bool read_sector(int lba, SectorKind kind, void* dst);

// How fast the drive model (audio/game DiscDrive) serves reads.
struct DrivePacing {
    // Set around a bounded drain of blocking loads: reads hand over everything at once and seeks are instant.
    bool instant = false;
    // Config cd_speed_ps1: reads take as long as on the console's 2x drive; off, every read is instant.
    bool console_speed = false;
};

DrivePacing& drive_pacing();

}  // namespace sh::disc
