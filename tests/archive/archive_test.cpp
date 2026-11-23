#include "framework/sh_test.h"
#include "formats/archive/sh_archive.h"
#include "formats/common/byte_source.h"
#include "formats/common/crc32.h"
#include "formats/disc/disc_image.h"
#include "formats/iso9660/iso9660.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

// Checks the SILENT. / HILL. file table against the real image. Expected sizes and CRC32 values are reference
// values of the files split from the image (docs/FORMATS.md).

namespace {

namespace fs = std::filesystem;
namespace arc = sh::archive;

using sh::disc::Image;
using sh::iso9660::Volume;

// The expected values come from zlib, so the module's CRC-32 is checked against them.
std::uint32_t crc32_of(const std::vector<std::uint8_t>& data) {
    return sh::crc32(data.data(), data.size());
}

std::uint32_t sectors_for(std::uint32_t size) {
    return (size + 2047) / 2048;
}

class MemorySource final : public sh::ByteSource {
public:
    explicit MemorySource(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

    std::uint64_t size() const override { return bytes_.size(); }

    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > bytes_.size() || count > bytes_.size() - offset) {
            return false;
        }
        std::memcpy(dst, bytes_.data() + offset, count);
        return true;
    }

private:
    std::vector<std::uint8_t> bytes_;
};

struct Disc {
    std::shared_ptr<const Image> image;
    arc::Layout silent_layout;
    arc::Layout hill_layout;
    std::shared_ptr<const sh::ByteSource> exe;
    std::shared_ptr<const sh::ByteSource> silent;
    std::shared_ptr<const sh::ByteSource> hill;
};

// Skips when the image is absent. The layouts come from the ISO9660 records; HILL. is ceil(size / 2048) sectors,
// the count the reference output was made from (the XA track names confirm it, see docs/FORMATS.md).
std::optional<Disc> open_disc() {
    const fs::path path = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
    if (!fs::exists(path)) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    const std::shared_ptr<const Image> image(Image::open(path));
    if (!image) {
        return std::nullopt;
    }
    const auto volume = Volume::open(image);
    if (!volume) {
        return std::nullopt;
    }
    const auto slus = volume->find("SLUS_007.07");
    const auto silent = volume->find("SILENT.");
    const auto hill = volume->find("HILL.");
    if (!slus || !silent || !hill) {
        return std::nullopt;
    }

    Disc disc;
    disc.image = image;
    disc.exe = std::shared_ptr<const sh::ByteSource>(volume->open_file(*slus));
    disc.silent = std::shared_ptr<const sh::ByteSource>(volume->open_file(*silent));
    disc.silent_layout = {silent->lba, sectors_for(silent->size)};
    disc.hill_layout = {hill->lba, sectors_for(hill->size)};
    disc.hill = arc::open_hill(image, disc.hill_layout);
    if (!disc.exe || !disc.silent || !disc.hill) {
        return std::nullopt;
    }
    return disc;
}

// Fails the test when the image is present but does not match the expected layout.
std::optional<Disc> disc_or_skip() {
    auto disc = open_disc();
    SH_CHECK(disc.has_value());
    return disc;
}

std::optional<std::vector<arc::Entry>> load_table(const Disc& disc) {
    return arc::read_file_table(*disc.exe, disc.silent_layout, disc.hill_layout);
}

struct NamedId {
    std::uint16_t id;
    std::string_view name;
};

constexpr std::array<NamedId, 8> kEnumNames = {{
    {0, "1ST/2ZANKO80.TIM"},
    {2, "1ST/BASE.VAB"},
    {67, "ANIM/HB_M1S04.ANM"},
    {1422, "CHARA/HERO.ILM"},
    {1730, "SND/SHOTGUN.VAB"},
    {1776, "TEST/RESIDENT.CMP"},
    {2044, "XA/05_02152"},
    {2073, "XA/ZZ_14239"},
}};

struct Expected {
    std::uint16_t id;
    std::uint32_t size;
    std::uint32_t crc32;
};

// Reference values. The 1ST/*.BIN overlays and BG/HP_SAFE1.BIN, BG/S__SAFE2.BIN are left out: their references are
// decrypted or decompressed, and the reader returns the bytes as the disc holds them.
constexpr std::array<Expected, 7> kSilentFiles = {{
    {0, 77568, 0x9FFF51AF},     // 1ST/2ZANKO80.TIM
    {1, 77568, 0xA0286D9F},     // 1ST/2ZANKO_E.TIM
    {2, 136448, 0x3EFA9747},    // 1ST/BASE.VAB
    {67, 0, 0x00000000},        // ANIM/HB_M1S04.ANM, zero-length: the next entry starts in the same sector
    {1422, 14592, 0xCADC3DAE},  // CHARA/HERO.ILM
    {1730, 27648, 0x9A4AB045},  // SND/SHOTGUN.VAB
    {1776, 31488, 0x4B61630F},  // TEST/RESIDENT.CMP, raw
}};

constexpr std::array<Expected, 5> kHillFiles = {{
    {2044, 5027072, 0xDED3232A},   // XA/05_02152
    {2045, 10353152, 0x9D2EB446},  // XA/10_04432
    {2046, 17510656, 0xF569781D},  // XA/15_07496
    {2047, 15305472, 0xEC63E6B3},  // XA/20_06552
    {2073, 33262304, 0x8E0AF556},  // XA/ZZ_14239, the last track runs to the end of HILL.
}};

static_assert(kSilentFiles.size() >= 5);
static_assert(kHillFiles.size() >= 5);

void check_files(const std::vector<arc::Entry>& table, std::span<const Expected> expected, const arc::Archive& archive) {
    for (const Expected& want : expected) {
        auto bytes = archive.read(table[want.id]);
        SH_CHECK(bytes.has_value());
        if (!bytes) {
            continue;
        }
        SH_CHECK(bytes->size() == want.size);
        SH_CHECK(crc32_of(*bytes) == want.crc32);
    }
}

}  // namespace

SH_TEST(Crc32MatchesCheckValue) {
    const std::string digits = "123456789";
    SH_CHECK(crc32_of(std::vector<std::uint8_t>(digits.begin(), digits.end())) == 0xCBF43926u);
    SH_CHECK(sh::crc32(nullptr, 0) == 0u);
}

SH_TEST(FileTableCoversBothContainers) {
    auto disc = disc_or_skip();
    if (!disc) {
        return;
    }
    auto table = load_table(*disc);
    SH_CHECK(table.has_value());
    if (!table) {
        return;
    }
    SH_CHECK(table->size() == 2074);

    std::set<std::string> names;
    std::size_t hill = 0;
    bool ids_in_order = true;
    for (std::size_t i = 0; i < table->size(); ++i) {
        const arc::Entry& e = (*table)[i];
        ids_in_order = ids_in_order && static_cast<std::size_t>(e.id) == i;
        names.insert(e.name);
        hill += e.region == arc::Region::Hill ? 1 : 0;
    }
    SH_CHECK(ids_in_order);
    SH_CHECK(names.size() == 2074);
    SH_CHECK(hill == 30);
    SH_CHECK((*table)[2043].region == arc::Region::Silent);
    SH_CHECK((*table)[2044].region == arc::Region::Hill);
}

SH_TEST(FileTableNamesMatchFileEnum) {
    auto disc = disc_or_skip();
    if (!disc) {
        return;
    }
    auto table = load_table(*disc);
    SH_CHECK(table.has_value());
    if (!table) {
        return;
    }
    for (const NamedId& want : kEnumNames) {
        SH_CHECK((*table)[want.id].name == want.name);
    }
}

SH_TEST(FileTableSizesMatchExtract) {
    auto disc = disc_or_skip();
    if (!disc) {
        return;
    }
    auto table = load_table(*disc);
    SH_CHECK(table.has_value());
    if (!table) {
        return;
    }
    for (const Expected& want : kSilentFiles) {
        SH_CHECK((*table)[want.id].size == want.size);
        SH_CHECK((*table)[want.id].region == arc::Region::Silent);
    }
    for (const Expected& want : kHillFiles) {
        SH_CHECK((*table)[want.id].size == want.size);
        SH_CHECK((*table)[want.id].region == arc::Region::Hill);
    }
}

SH_TEST(FileTableBytesMatchExtract) {
    auto disc = disc_or_skip();
    if (!disc) {
        return;
    }
    auto table = load_table(*disc);
    SH_CHECK(table.has_value());
    if (!table) {
        return;
    }
    auto silent = arc::Archive::open(disc->silent, arc::Region::Silent);
    auto hill = arc::Archive::open(disc->hill, arc::Region::Hill);
    SH_CHECK(silent.has_value() && hill.has_value());
    if (!silent || !hill) {
        return;
    }
    check_files(*table, kSilentFiles, *silent);
    check_files(*table, kHillFiles, *hill);
}

SH_TEST(HillStreamHasTwoThousandThreeHundredThirtySixBytesPerSector) {
    auto disc = disc_or_skip();
    if (!disc) {
        return;
    }
    SH_CHECK(disc->hill->size() == std::uint64_t{disc->hill_layout.sector_count} * 2336);
    SH_CHECK(disc->hill_layout.sector_count == 222606);
}

SH_TEST(FileTableRejectsOtherRelease) {
    const arc::Layout silent{64, 39295};
    const arc::Layout hill{39359, 222606};
    SH_CHECK(!arc::read_file_table(MemorySource(std::vector<std::uint8_t>(0x10000)), silent, hill).has_value());
    SH_CHECK(!arc::read_file_table(MemorySource(std::vector<std::uint8_t>(100)), silent, hill).has_value());
}

SH_TEST(ArchiveReadsOnlyItsOwnRegion) {
    std::vector<std::uint8_t> bytes(100);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(i);
    }
    const auto source = std::make_shared<const MemorySource>(bytes);
    auto archive = arc::Archive::open(source, arc::Region::Silent);
    SH_CHECK(archive.has_value());
    if (!archive) {
        return;
    }

    const arc::Entry silent_entry{7, "TEST/X.TIM", arc::Region::Silent, 10, 20, 0, 1};
    const arc::Entry hill_entry{8, "XA/X", arc::Region::Hill, 10, 20, 0, 1};
    auto data = archive->read(silent_entry);
    SH_CHECK(data.has_value());
    if (data) {
        SH_CHECK(data->size() == 20);
        SH_CHECK(data->front() == 10);
        SH_CHECK(data->back() == 29);
    }
    SH_CHECK(!archive->read(hill_entry).has_value());
    SH_CHECK(!arc::Archive::open(nullptr, arc::Region::Silent).has_value());
}

SH_TEST(ReadEntryChecksBounds) {
    std::vector<std::uint8_t> bytes(100);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(i);
    }
    const MemorySource container(bytes);

    const arc::Entry past_end{8, "TEST/Y.TIM", arc::Region::Silent, 90, 20, 0, 1};
    SH_CHECK(!arc::read_entry(container, past_end).has_value());

    const arc::Entry empty{9, "TEST/Z.TIM", arc::Region::Silent, 500, 0, 0, 0};
    auto none = arc::read_entry(container, empty);
    SH_CHECK(none.has_value() && none->empty());
}

// read_entry gives the logical size and the bytes as they lie on disc: the overlay stays encrypted, the game decrypts
// it itself (docs/FORMATS.md). The CRC32 is computed from the image.
SH_TEST(EncryptedOverlayIsReadAsOnDisc) {
    auto disc = disc_or_skip();
    if (!disc) {
        return;
    }
    auto table = load_table(*disc);
    SH_CHECK(table.has_value());
    if (!table) {
        return;
    }
    auto silent = arc::Archive::open(disc->silent, arc::Region::Silent);
    SH_CHECK(silent.has_value());
    if (!silent) {
        return;
    }
    const arc::Entry& overlay = (*table)[3];
    SH_CHECK(overlay.name == "1ST/BODYPROG.BIN");
    auto bytes = silent->read(overlay);
    SH_CHECK(bytes.has_value());
    if (bytes) {
        SH_CHECK(bytes->size() == 674560);
        SH_CHECK(crc32_of(*bytes) == 0xA8966201u);
    }
}

// The stored file of an entry is sectors × 2048 bytes: the logical bytes plus the tail up to the sector boundary.
// read_entry does not return the tail, but the container holds it. The CRC32 values are the manifest samples of
// docs/FORMATS.md, section 5, checked against the image.
SH_TEST(StoredSectorsKeepTheTail) {
    auto disc = disc_or_skip();
    if (!disc) {
        return;
    }
    auto table = load_table(*disc);
    SH_CHECK(table.has_value());
    if (!table) {
        return;
    }
    struct Sample {
        std::uint16_t id;
        std::uint32_t crc32;
    };
    constexpr std::array<Sample, 2> samples = {{{3, 0xAC9CDBC1u}, {1422, 0x2D30905Cu}}};
    for (const Sample& want : samples) {
        const arc::Entry& entry = (*table)[want.id];
        std::vector<std::uint8_t> bytes(std::uint64_t{(entry.size + 2047) / 2048} * 2048);
        const bool ok = disc->silent->read_at(entry.offset, bytes.data(), bytes.size());
        SH_CHECK(ok);
        if (ok) {
            SH_CHECK(crc32_of(bytes) == want.crc32);
        }
    }
}

// lba and sectors follow docs/FORMATS.md, section 5: the first disc sector of the entry, and
// ceil(size / sector size) sectors. The samples are the examples of that section.
SH_TEST(EntriesCarryLbaAndSectors) {
    auto disc = disc_or_skip();
    if (!disc) {
        return;
    }
    auto table = load_table(*disc);
    SH_CHECK(table.has_value());
    if (!table) {
        return;
    }
    bool consistent = true;
    for (const arc::Entry& e : *table) {
        const bool silent = e.region == arc::Region::Silent;
        const arc::Layout& layout = silent ? disc->silent_layout : disc->hill_layout;
        const std::uint64_t sector = silent ? 2048 : 2336;
        consistent = consistent && e.offset % sector == 0 && e.lba == layout.start_lba + e.offset / sector &&
                     e.sectors == (e.size + sector - 1) / sector;
    }
    SH_CHECK(consistent);

    struct Sample {
        std::uint16_t id;
        std::uint32_t lba;
        std::uint32_t sectors;
        std::uint32_t size;
    };
    constexpr std::array<Sample, 4> samples = {{
        {1422, 23138, 8, 14592},       // CHARA/HERO.ILM
        {3, 207, 330, 674560},         // 1ST/BODYPROG.BIN
        {2038, 39292, 0, 0},           // VIN/MAPT_S00.BIN, empty
        {2044, 39359, 2152, 5027072},  // XA/05_02152
    }};
    for (const Sample& want : samples) {
        const arc::Entry& e = (*table)[want.id];
        SH_CHECK(e.lba == want.lba);
        SH_CHECK(e.sectors == want.sectors);
        SH_CHECK(e.size == want.size);
    }
}
