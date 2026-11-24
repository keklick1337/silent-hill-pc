#include "framework/sh_test.h"
#include "formats/common/byte_source.h"
#include "formats/disc/disc_image.h"
#include "formats/disc/zip_archive.h"
#include "import/input.h"
#include "import/ui/input_check.h"

#include "miniz/include/miniz.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kRaw = 2352;
// Enough sectors for several 2 MiB spans of the inflater's restart points.
constexpr std::uint32_t kSectors = 3000;

fs::path scratch_dir(const std::string& name) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "zip" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

// Sync pattern, then bytes of a generator seeded by the sector: little redundancy, so the deflate stream is long and
// back-references cross the restart points.
std::vector<std::uint8_t> synthetic_disc(std::uint32_t sectors) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(sectors) * kRaw);
    std::uint32_t state = 12345;
    for (std::uint32_t lba = 0; lba < sectors; ++lba) {
        std::uint8_t* s = bytes.data() + static_cast<std::size_t>(lba) * kRaw;
        s[0] = 0x00;
        std::memset(s + 1, 0xff, 10);
        s[11] = 0x00;
        for (std::size_t i = 12; i < kRaw; ++i) {
            state = state * 1103515245u + 12345u;
            // Every other run repeats, so the compressor finds matches as well as literals.
            s[i] = (i / 64) % 2 ? static_cast<std::uint8_t>(lba + i / 64) : static_cast<std::uint8_t>(state >> 24);
        }
    }
    return bytes;
}

struct Member {
    std::string name;
    std::vector<std::uint8_t> bytes;
    unsigned level;  // MZ_NO_COMPRESSION stores the member
};

bool write_zip(const fs::path& path, const std::vector<Member>& members) {
    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, path.string().c_str(), 0)) {
        return false;
    }
    bool ok = true;
    for (const Member& m : members) {
        ok = ok && mz_zip_writer_add_mem(&zip, m.name.c_str(), m.bytes.data(), m.bytes.size(), m.level);
    }
    ok = mz_zip_writer_finalize_archive(&zip) && ok;
    return mz_zip_writer_end(&zip) && ok;
}

std::vector<std::uint8_t> text(const std::string& s) { return {s.begin(), s.end()}; }

std::string cue_sheet(const std::string& file) {
    return "FILE \"" + file + "\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n";
}

bool sector_matches(const sh::disc::Image& image, const std::vector<std::uint8_t>& disc, std::uint32_t lba) {
    std::uint8_t raw[kRaw];
    return image.read_raw(lba, raw) && std::memcmp(raw, disc.data() + static_cast<std::size_t>(lba) * kRaw, kRaw) == 0;
}

}  // namespace

SH_TEST(zip_deflated_cue_and_bin_read_in_any_order) {
    const fs::path dir = scratch_dir("deflated");
    const std::vector<std::uint8_t> disc = synthetic_disc(kSectors);
    const fs::path zip = dir / "game.zip";
    SH_CHECK(write_zip(zip, {{"Silent Hill/Game (v1.1).cue", text(cue_sheet("Game (v1.1).bin")), MZ_BEST_SPEED},
                             {"Silent Hill/Game (v1.1).bin", disc, MZ_BEST_SPEED}}));

    sh::disc::ZipImage zipped = sh::disc::open_zip_image(zip);
    SH_CHECK(zipped.ok());
    SH_CHECK(zipped.cue == "Silent Hill/Game (v1.1).cue");
    SH_CHECK(zipped.member == "Silent Hill/Game (v1.1).bin");
    SH_CHECK(zipped.size == disc.size());

    const std::unique_ptr<sh::disc::Image> image = sh::disc::Image::open(zip);
    SH_CHECK(image != nullptr);
    if (!image) {
        return;
    }
    SH_CHECK(image->sector_count() == kSectors);
    SH_CHECK(image->sync_ok());
    // The last sector first: the restart points are made by one read-through, then reads jump back and forth.
    SH_CHECK(sector_matches(*image, disc, kSectors - 1));
    bool all = true;
    for (std::uint32_t lba = 0; lba < kSectors; lba += 37) {
        all = all && sector_matches(*image, disc, kSectors - 1 - lba) && sector_matches(*image, disc, lba);
    }
    SH_CHECK(all);
    std::uint8_t raw[kRaw];
    SH_CHECK(!image->read_raw(kSectors, raw));

    // Several readers at once, as the import's worker threads read.
    std::atomic<bool> threads_ok{true};
    std::vector<std::thread> readers;
    for (std::uint32_t t = 0; t < 4; ++t) {
        readers.emplace_back([&, t] {
            for (std::uint32_t lba = t; lba < kSectors; lba += 13) {
                if (!sector_matches(*image, disc, (lba * 7919u) % kSectors)) {
                    threads_ok = false;
                }
            }
        });
    }
    for (std::thread& r : readers) {
        r.join();
    }
    SH_CHECK(threads_ok.load());
}

SH_TEST(zip_stored_bin_is_read_in_place) {
    const fs::path dir = scratch_dir("stored");
    const std::vector<std::uint8_t> disc = synthetic_disc(40);
    const fs::path zip = dir / "game.zip";
    SH_CHECK(
        write_zip(zip, {{"readme.txt", text("not an image"), MZ_BEST_SPEED}, {"GAME.BIN", disc, MZ_NO_COMPRESSION}}));

    const std::unique_ptr<sh::disc::ZipArchive> archive = sh::disc::ZipArchive::open(zip);
    SH_CHECK(archive != nullptr);
    if (!archive) {
        return;
    }
    SH_CHECK(archive->members().size() == 2);
    SH_CHECK(archive->find("game.bin").has_value());
    SH_CHECK(!archive->find("game.cue").has_value());

    const std::unique_ptr<sh::disc::Image> image = sh::disc::Image::open(zip);
    SH_CHECK(image != nullptr);
    if (!image) {
        return;
    }
    SH_CHECK(image->sector_count() == 40);
    bool all = true;
    for (std::uint32_t lba = 40; lba-- > 0;) {
        all = all && sector_matches(*image, disc, lba);
    }
    SH_CHECK(all);
}

SH_TEST(zip_long_runs_cross_the_restart_points) {
    // Zeros compress to back-references that reach across every 2 MiB restart point.
    const fs::path dir = scratch_dir("zeros");
    std::vector<std::uint8_t> bytes(5u << 20);
    for (std::size_t i = 0; i < bytes.size(); i += 4093) {
        bytes[i] = static_cast<std::uint8_t>(i >> 12);
    }
    const fs::path zip = dir / "data.zip";
    SH_CHECK(write_zip(zip, {{"data", bytes, MZ_BEST_COMPRESSION}}));
    const std::unique_ptr<sh::disc::ZipArchive> archive = sh::disc::ZipArchive::open(zip);
    SH_CHECK(archive != nullptr);
    if (!archive) {
        return;
    }
    const std::unique_ptr<sh::ByteSource> source = archive->open_member(0);
    SH_CHECK(source != nullptr && source->size() == bytes.size());
    if (!source) {
        return;
    }
    std::vector<std::uint8_t> got(bytes.size());
    // Backwards in odd pieces, then across the end.
    bool all = true;
    for (std::size_t end = bytes.size(); end > 0;) {
        const std::size_t n = std::min<std::size_t>(end, 300007);
        all = all && source->read_at(end - n, got.data() + end - n, n);
        end -= n;
    }
    SH_CHECK(all);
    SH_CHECK(got == bytes);
    SH_CHECK(!source->read_at(bytes.size() - 2, got.data(), 3));
}

SH_TEST(zip_problems_are_named) {
    const fs::path dir = scratch_dir("problems");
    const fs::path no_image = dir / "docs.zip";
    SH_CHECK(write_zip(no_image, {{"readme.txt", text("hello"), MZ_BEST_SPEED}}));
    SH_CHECK(sh::disc::open_zip_image(no_image).status == sh::disc::ZipImage::Status::NoImage);

    const fs::path missing = dir / "missing.zip";
    SH_CHECK(write_zip(missing, {{"game.cue", text(cue_sheet("other.bin")), MZ_BEST_SPEED}}));
    const sh::disc::ZipImage m = sh::disc::open_zip_image(missing);
    SH_CHECK(m.status == sh::disc::ZipImage::Status::CueTargetMissing);
    SH_CHECK(m.cue_target == "other.bin");

    // A cue whose target is missing does not hide a .bin the archive does hold.
    const fs::path fallback = dir / "fallback.zip";
    SH_CHECK(write_zip(fallback, {{"a.cue", text(cue_sheet("gone.bin")), MZ_BEST_SPEED},
                                  {"b.bin", synthetic_disc(2), MZ_BEST_SPEED}}));
    const sh::disc::ZipImage f = sh::disc::open_zip_image(fallback);
    SH_CHECK(f.ok() && f.member == "b.bin" && f.cue.empty());

    const fs::path not_zip = dir / "fake.zip";
    {
        std::ofstream out(not_zip, std::ios::binary);
        out << "PK\x03\x04 and nothing of an archive";
    }
    SH_CHECK(sh::disc::open_zip_image(not_zip).status == sh::disc::ZipImage::Status::NotZip);
    SH_CHECK(sh::disc::Image::open(not_zip) == nullptr);
}

SH_TEST(zip_accepted_by_the_import_checks) {
    const fs::path dir = scratch_dir("import");
    const std::vector<std::uint8_t> disc = synthetic_disc(30);
    const fs::path zip = dir / "Silent Hill.ZIP";
    SH_CHECK(write_zip(zip, {{"sh.cue", text(cue_sheet("sh.bin")), MZ_BEST_SPEED}, {"sh.bin", disc, MZ_BEST_SPEED}}));

    const sh::import::ui::ImageCheck check = sh::import::ui::check_image(zip.string());
    SH_CHECK(check.ok());
    SH_CHECK(check.member == "sh.bin");
    SH_CHECK(check.size == disc.size());

    const auto image = sh::import::open_image(zip, dir / "alias.bin");
    SH_CHECK(static_cast<bool>(image));
    if (image) {
        SH_CHECK(image.value()->sector_count() == 30);
    }

    // A zipped 2048-byte .iso is refused like a loose one.
    const fs::path cooked = dir / "cooked.zip";
    SH_CHECK(write_zip(cooked, {{"game.iso", std::vector<std::uint8_t>(20 * 2048, 0), MZ_BEST_SPEED}}));
    SH_CHECK(sh::import::ui::check_image(cooked.string()).problem == sh::import::ui::Problem::CookedIso);
    SH_CHECK(!sh::import::open_image(cooked, dir / "alias.bin"));

    const fs::path empty = dir / "empty.zip";
    SH_CHECK(write_zip(empty, {{"notes.txt", text("x"), MZ_BEST_SPEED}}));
    SH_CHECK(sh::import::ui::check_image(empty.string()).problem == sh::import::ui::Problem::ZipNoImage);
}
