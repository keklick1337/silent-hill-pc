#include "formats/archive/sh_archive.h"

#include "formats/common/crc32.h"
#include "formats/disc/disc_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace sh::archive {
namespace {

// File offset inside SLUS_007.07 (the PS-EXE file), not a memory address.
constexpr std::uint32_t kTableOffset = 0xB91C;
constexpr std::uint32_t kTableCount = 2074;
constexpr std::uint32_t kTableEntryBytes = 12;
constexpr std::size_t kReleaseCheckBytes = 4096;
constexpr std::uint32_t kReleaseCrc32 = 0xCF9CD8E5;

constexpr std::uint64_t kBlockBytes = 256;
constexpr std::uint64_t kSilentSectorBytes = 2048;
constexpr std::uint64_t kHillSectorBytes = 2336;
constexpr std::size_t kRawSectorBytes = 2352;
constexpr std::size_t kHillPayloadOffset = 16;
constexpr std::uint32_t kXaDir = 10;

constexpr std::array<const char*, 11> kDirs = {
    "1ST", "ANIM", "BG", "CHARA", "ITEM", "MISC", "SND", "TEST", "TIM", "VIN", "XA"};
constexpr std::array<const char*, 16> kTypes = {
    "TIM", "VAB", "BIN", "DMS", "ANM", "PLM", "IPD", "ILM",
    "TMD", "DAT", "KDT", "CMP", "TXT", "UU1", "UU2", ""};

std::uint32_t load_le32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
           (std::uint32_t{p[3]} << 24);
}

// Six bits per character, stored as 32 + value. A value of 0 is a space, which is how short names are padded.
std::string decode_name(std::uint32_t file1, std::uint32_t file2) {
    std::string name;
    for (int shift = 4; shift < 28; shift += 6) {
        name += static_cast<char>(32 + ((file1 >> shift) & 63));
    }
    for (int shift = 0; shift < 24; shift += 6) {
        name += static_cast<char>(32 + ((file2 >> shift) & 63));
    }
    const std::size_t first = name.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    return name.substr(first, name.find_last_not_of(' ') - first + 1);
}

struct RawEntry {
    std::uint32_t lba;
    std::uint32_t blocks;  // the table's size field, in 256-byte units
    bool has_type;         // XA tracks have no type and their size field is not used
};

// HILL. payload: 2336 bytes from raw offset 16 of each sector. The ISO 9660 reader returns only Form 1 user data,
// so the stream is built from the raw sectors.
class HillPayload final : public ByteSource {
public:
    HillPayload(std::shared_ptr<const disc::Image> image, std::uint32_t first_lba, std::uint32_t sectors)
        : image_(std::move(image)), first_lba_(first_lba), sectors_(sectors) {}

    std::uint64_t size() const override { return std::uint64_t{sectors_} * kHillSectorBytes; }

    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > size() || count > size() - offset) {
            return false;
        }
        auto* out = static_cast<std::uint8_t*>(dst);
        std::array<std::uint8_t, kRawSectorBytes> raw{};
        while (count > 0) {
            const auto sector = static_cast<std::uint32_t>(offset / kHillSectorBytes);
            const auto inside = static_cast<std::size_t>(offset % kHillSectorBytes);
            const std::size_t n = std::min<std::size_t>(count, kHillSectorBytes - inside);
            if (!image_->read_raw(first_lba_ + sector, raw.data())) {
                return false;
            }
            std::memcpy(out, raw.data() + kHillPayloadOffset + inside, n);
            out += n;
            offset += n;
            count -= n;
        }
        return true;
    }

private:
    std::shared_ptr<const disc::Image> image_;
    std::uint32_t first_lba_;
    std::uint32_t sectors_;
};

}  // namespace

std::optional<std::vector<Entry>> read_file_table(const ByteSource& exe, Layout silent, Layout hill) {
    if (exe.size() < std::uint64_t{kTableOffset} + std::uint64_t{kTableEntryBytes} * kTableCount) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> head(kReleaseCheckBytes);
    if (!exe.read_at(0, head.data(), head.size()) || sh::crc32(head.data(), head.size()) != kReleaseCrc32) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> table(std::size_t{kTableEntryBytes} * kTableCount);
    if (!exe.read_at(kTableOffset, table.data(), table.size())) {
        return std::nullopt;
    }

    std::vector<Entry> entries(kTableCount);
    std::vector<RawEntry> raw(kTableCount);
    for (std::uint32_t i = 0; i < kTableCount; ++i) {
        const std::uint8_t* p = table.data() + std::size_t{i} * kTableEntryBytes;
        const std::uint32_t meta = load_le32(p);
        const std::uint32_t file1 = load_le32(p + 4);
        const std::uint32_t file2 = load_le32(p + 8);

        const std::uint32_t dir = file1 & 15;
        if (dir >= kDirs.size()) {
            return std::nullopt;
        }
        const std::string type = kTypes[(file2 >> 24) & 15];
        const std::string name = decode_name(file1, file2);

        Entry& e = entries[i];
        e.id = static_cast<std::uint16_t>(i);
        e.name = std::string(kDirs[dir]) + "/" + name + (type.empty() ? std::string() : "." + type);
        e.region = dir == kXaDir ? Region::Hill : Region::Silent;
        raw[i] = RawEntry{meta & 0x7FFFF, meta >> 19, !type.empty()};
    }

    // Sizes come from the next entry of the same container, so walk backwards and keep the next start.
    std::array<std::uint32_t, 2> next = {silent.start_lba + silent.sector_count, hill.start_lba + hill.sector_count};
    for (std::size_t i = kTableCount; i-- > 0;) {
        Entry& e = entries[i];
        const RawEntry& r = raw[i];
        const std::size_t c = e.region == Region::Silent ? 0 : 1;
        const Layout& layout = c == 0 ? silent : hill;
        const std::uint64_t sector = c == 0 ? kSilentSectorBytes : kHillSectorBytes;
        std::uint32_t& next_lba = next[c];
        if (r.lba < layout.start_lba || r.lba > next_lba) {
            return std::nullopt;
        }

        const std::uint64_t size = r.has_type && r.blocks != 0
                                       ? std::uint64_t{r.blocks} * kBlockBytes
                                       : (std::uint64_t{next_lba} - r.lba) * sector;
        const std::uint64_t sectors = (size + sector - 1) / sector;
        if (size > std::numeric_limits<std::uint32_t>::max() || r.lba + sectors > next_lba) {
            return std::nullopt;
        }

        e.lba = r.lba;
        e.offset = std::uint64_t{r.lba - layout.start_lba} * sector;
        e.size = static_cast<std::uint32_t>(size);
        e.sectors = static_cast<std::uint32_t>(sectors);
        next_lba = r.lba;
    }
    return entries;
}

std::unique_ptr<ByteSource> open_hill(std::shared_ptr<const disc::Image> image, Layout hill) {
    if (!image) {
        return nullptr;
    }
    return std::make_unique<HillPayload>(std::move(image), hill.start_lba, hill.sector_count);
}

std::optional<std::vector<std::uint8_t>> read_entry(const ByteSource& container, const Entry& entry) {
    if (entry.size == 0) {
        return std::vector<std::uint8_t>{};
    }
    if (entry.offset > container.size() || entry.size > container.size() - entry.offset) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> data(entry.size);
    if (!container.read_at(entry.offset, data.data(), data.size())) {
        return std::nullopt;
    }
    return data;
}

std::optional<Archive> Archive::open(std::shared_ptr<const ByteSource> source, Region region) {
    if (!source) {
        return std::nullopt;
    }
    return Archive(std::move(source), region);
}

Archive::Archive(std::shared_ptr<const ByteSource> source, Region region)
    : source_(std::move(source)), region_(region) {}

std::optional<std::vector<std::uint8_t>> Archive::read(const Entry& entry) const {
    if (entry.region != region_) {
        return std::nullopt;
    }
    return read_entry(*source_, entry);
}

}  // namespace sh::archive
