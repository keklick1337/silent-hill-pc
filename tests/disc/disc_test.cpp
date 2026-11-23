#include "framework/sh_test.h"
#include "formats/common/byte_source.h"
#include "formats/disc/disc_image.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;

using sh::disc::Image;

constexpr std::size_t kRaw = 2352;
constexpr std::size_t kUser = 2048;

fs::path real_bin() {
    return fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
}

fs::path real_cue() {
    return fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).cue";
}

// Synthetic files go to .tmp/disc, which git ignores.
fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "disc";
    fs::create_directories(dir);
    return dir / name;
}

void write_bytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// Removes the file when the test ends, also when a check fails.
struct Cleanup {
    fs::path path;
    ~Cleanup() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// Byte i of the user data of sector lba. Each sector differs, so a wrong LBA shows up as a mismatch.
std::uint8_t user_byte(std::uint32_t lba, std::size_t i) {
    return static_cast<std::uint8_t>(lba * 7 + i);
}

// Sync pattern, mode 2, zero subheader and EDC/ECC, user data from user_byte().
std::string raw_sector(std::uint32_t lba) {
    std::string raw(kRaw, '\0');
    for (std::size_t i = 1; i <= 10; ++i) {
        raw[i] = '\xff';
    }
    raw[15] = 2;
    for (std::size_t i = 0; i < kUser; ++i) {
        raw[24 + i] = static_cast<char>(user_byte(lba, i));
    }
    return raw;
}

void write_disc(const fs::path& path, std::uint32_t sectors) {
    std::string bytes;
    for (std::uint32_t lba = 0; lba < sectors; ++lba) {
        bytes += raw_sector(lba);
    }
    write_bytes(path, bytes);
}

// A one-track cue sheet with CRLF line ends, as on the real disc.
std::string cue_sheet(const std::string& file, const std::string& track, const std::string& index) {
    return "FILE \"" + file + "\" BINARY\r\n  TRACK 01 " + track + "\r\n    INDEX 01 " + index + "\r\n";
}

bool matches_synthetic(const Image& image, std::uint32_t lba) {
    std::uint8_t raw[kRaw];
    std::uint8_t user[kUser];
    if (!image.read_raw(lba, raw) || !image.read_user_data(lba, user)) {
        return false;
    }
    return std::memcmp(raw, raw_sector(lba).data(), kRaw) == 0 &&
           std::memcmp(user, raw + 24, kUser) == 0;
}

}  // namespace

SH_TEST(byte_source_checks_bounds_without_overflow) {
    const Cleanup file{scratch("bounds.bin")};
    std::string bytes;
    for (int i = 0; i < 100; ++i) {
        bytes.push_back(static_cast<char>('A' + i % 26));
    }
    write_bytes(file.path, bytes);

    auto source = sh::open_file_source(file.path);
    SH_CHECK(source != nullptr);
    if (!source) {
        return;
    }
    SH_CHECK(source->size() == 100);
    char buf[16] = {};
    SH_CHECK(source->read_at(10, buf, 4));
    SH_CHECK(std::memcmp(buf, "KLMN", 4) == 0);
    SH_CHECK(source->read_at(84, buf, 16));
    SH_CHECK(!source->read_at(85, buf, 16));
    SH_CHECK(source->read_at(100, buf, 0));
    SH_CHECK(!source->read_at(101, buf, 0));
    SH_CHECK(!source->read_at(std::numeric_limits<std::uint64_t>::max() - 3, buf, 8));
}

SH_TEST(byte_source_reports_short_read_after_truncation) {
    const Cleanup file{scratch("truncated.bin")};
    write_bytes(file.path, std::string(100, 'x'));

    auto source = sh::open_file_source(file.path);
    SH_CHECK(source != nullptr);
    if (!source) {
        return;
    }
    fs::resize_file(file.path, 50);
    char buf[10] = {};
    // The size was taken at open, so the request is in range, but the file now ends at 50.
    SH_CHECK(!source->read_at(60, buf, 10));
    SH_CHECK(source->size() == 100);
}

SH_TEST(byte_source_uses_64bit_offsets) {
    // Sparse file: only the marker byte is written, the rest is a hole on filesystems that support one.
    constexpr std::uint64_t kMarker = (std::uint64_t{1} << 32) + 1234;
    constexpr std::uint64_t kSize = (std::uint64_t{1} << 32) + 4096;
    const Cleanup file{scratch("sparse_over_4gib.bin")};
    {
        std::ofstream out(file.path, std::ios::binary | std::ios::trunc);
        out.seekp(static_cast<std::streamoff>(kMarker));
        out.put('Q');
    }
    fs::resize_file(file.path, kSize);

    auto source = sh::open_file_source(file.path);
    SH_CHECK(source != nullptr);
    if (!source) {
        return;
    }
    SH_CHECK(source->size() == kSize);
    char c = 0;
    SH_CHECK(source->read_at(kMarker, &c, 1) && c == 'Q');
    SH_CHECK(source->read_at(kSize - 1, &c, 1) && c == '\0');
    SH_CHECK(!source->read_at(kSize, &c, 1));
}

SH_TEST(byte_source_serves_concurrent_reads) {
    const Cleanup file{scratch("concurrent.bin")};
    std::string bytes(1 << 20, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>(i * 31 + i / 256);
    }
    write_bytes(file.path, bytes);

    auto source = sh::open_file_source(file.path);
    SH_CHECK(source != nullptr);
    if (!source) {
        return;
    }
    std::atomic<int> bad_reads{0};
    std::vector<std::thread> threads;
    for (std::size_t t = 0; t < 4; ++t) {
        threads.emplace_back([&source, &bytes, &bad_reads, t] {
            std::vector<std::uint8_t> buf(4096);
            for (std::size_t round = 0; round < 200; ++round) {
                const std::size_t offset = (round * 4099 + t * 7919) % (bytes.size() - buf.size());
                if (!source->read_at(offset, buf.data(), buf.size()) ||
                    std::memcmp(buf.data(), bytes.data() + offset, buf.size()) != 0) {
                    ++bad_reads;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    SH_CHECK(bad_reads.load() == 0);
}

SH_TEST(byte_source_is_null_for_missing_file_and_directory) {
    SH_CHECK(sh::open_file_source(scratch("no_such_file.bin")) == nullptr);
    SH_CHECK(sh::open_file_source(scratch("")) == nullptr);
}

SH_TEST(disc_real_image_layout) {
    if (!fs::exists(real_bin())) {
        SH_SKIP("data/Silent Hill (v1.1).bin is missing");
    }
    auto image = Image::open(real_bin());
    SH_CHECK(image != nullptr);
    if (!image) {
        return;
    }
    SH_CHECK(image->sector_count() == 262115);
    SH_CHECK(image->sync_ok());

    std::uint8_t raw[kRaw];
    std::uint8_t user[kUser];
    SH_CHECK(image->read_raw(16, raw));
    SH_CHECK(raw[15] == 2);
    SH_CHECK(image->read_user_data(16, user));
    SH_CHECK(std::memcmp(raw + 24, user, kUser) == 0);
    SH_CHECK(user[0] == 1 && std::memcmp(user + 1, "CD001", 5) == 0);
    SH_CHECK(image->read_user_data(23, user));
    SH_CHECK(std::memcmp(user, "BOOT = cdrom:", 13) == 0);
    SH_CHECK(image->read_user_data(24, user));
    SH_CHECK(std::memcmp(user, "PS-X EXE", 8) == 0);
    SH_CHECK(image->read_user_data(262114, user));
    SH_CHECK(!image->read_user_data(262115, user));
    SH_CHECK(!image->read_raw(262115, raw));
}

SH_TEST(disc_real_cue_matches_bin) {
    if (!fs::exists(real_cue())) {
        SH_SKIP("data/Silent Hill (v1.1).cue is missing");
    }
    auto from_bin = Image::open(real_bin());
    auto from_cue = Image::open(real_cue());
    SH_CHECK(from_bin != nullptr && from_cue != nullptr);
    if (!from_bin || !from_cue) {
        return;
    }
    SH_CHECK(from_cue->sector_count() == from_bin->sector_count());
    const std::array<std::uint32_t, 5> lbas{0, 16, 22, 24, 262114};
    for (std::uint32_t lba : lbas) {
        std::uint8_t a[kRaw];
        std::uint8_t b[kRaw];
        SH_CHECK(from_bin->read_raw(lba, a) && from_cue->read_raw(lba, b));
        SH_CHECK(std::memcmp(a, b, kRaw) == 0);
    }
}

SH_TEST(disc_cue_opens_first_track_with_bom_and_quoted_name) {
    const Cleanup bin{scratch("cue track.bin")};
    const Cleanup cue{scratch("cue track.cue")};
    write_disc(bin.path, 4);
    write_bytes(cue.path, "\xEF\xBB\xBF" + cue_sheet("cue track.bin", "MODE2/2352", "00:00:00"));

    auto image = Image::open(cue.path);
    SH_CHECK(image != nullptr);
    if (!image) {
        return;
    }
    SH_CHECK(image->sector_count() == 4);
    SH_CHECK(image->sync_ok());
    for (std::uint32_t lba = 0; lba < 4; ++lba) {
        SH_CHECK(matches_synthetic(*image, lba));
    }
    std::uint8_t raw[kRaw];
    std::uint8_t user[kUser];
    SH_CHECK(!image->read_user_data(4, user));
    SH_CHECK(!image->read_raw(4, raw));
}

SH_TEST(disc_bin_opens_directly) {
    const Cleanup bin{scratch("direct.bin")};
    write_disc(bin.path, 4);

    auto image = Image::open(bin.path);
    SH_CHECK(image != nullptr);
    if (!image) {
        return;
    }
    SH_CHECK(image->sector_count() == 4);
    SH_CHECK(matches_synthetic(*image, 3));
}

SH_TEST(disc_cue_uses_first_track_only) {
    const Cleanup bin{scratch("multi.bin")};
    const Cleanup cue{scratch("multi.cue")};
    write_disc(bin.path, 4);
    write_bytes(cue.path,
                "FILE \"multi.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n"
                "FILE \"audio.wav\" WAVE\r\n  TRACK 02 AUDIO\r\n    INDEX 01 00:00:00\r\n");

    auto image = Image::open(cue.path);
    SH_CHECK(image != nullptr);
    if (!image) {
        return;
    }
    SH_CHECK(image->sector_count() == 4);
}

SH_TEST(disc_rejects_size_not_multiple_of_2352) {
    const Cleanup bin{scratch("bad_size.bin")};
    const Cleanup cue{scratch("bad_size.cue")};
    write_bytes(bin.path, std::string(2 * kRaw + 1, '\0'));
    write_bytes(cue.path, cue_sheet("bad_size.bin", "MODE2/2352", "00:00:00"));

    SH_CHECK(Image::open(bin.path) == nullptr);
    SH_CHECK(Image::open(cue.path) == nullptr);
}

SH_TEST(disc_rejects_missing_files) {
    const Cleanup cue{scratch("dangling.cue")};
    write_bytes(cue.path, cue_sheet("absent_track.bin", "MODE2/2352", "00:00:00"));

    SH_CHECK(Image::open(scratch("no_such_disc.bin")) == nullptr);
    SH_CHECK(Image::open(cue.path) == nullptr);
}

SH_TEST(disc_rejects_unsupported_sheets_and_extensions) {
    const Cleanup bin{scratch("unsupported.bin")};
    const Cleanup cue{scratch("unsupported.cue")};
    const Cleanup iso{scratch("unsupported.iso")};
    write_disc(bin.path, 4);
    write_bytes(iso.path, std::string(4 * kUser, '\0'));

    SH_CHECK(Image::open(iso.path) == nullptr);

    write_bytes(cue.path, cue_sheet("unsupported.bin", "AUDIO", "00:00:00"));
    SH_CHECK(Image::open(cue.path) == nullptr);

    write_bytes(cue.path, cue_sheet("unsupported.bin", "MODE2/2352", "00:02:00"));
    SH_CHECK(Image::open(cue.path) == nullptr);

    write_bytes(cue.path, "FILE \"unsupported.bin\" WAVE\r\n  TRACK 01 MODE2/2352\r\n"
                          "    INDEX 01 00:00:00\r\n");
    SH_CHECK(Image::open(cue.path) == nullptr);
}

SH_TEST(disc_bad_sync_is_reported_not_fatal) {
    const Cleanup bin{scratch("no_sync.bin")};
    write_bytes(bin.path, std::string(2 * kRaw, '\0'));

    auto image = Image::open(bin.path);
    SH_CHECK(image != nullptr);
    if (!image) {
        return;
    }
    SH_CHECK(image->sector_count() == 2);
    SH_CHECK(!image->sync_ok());
}
