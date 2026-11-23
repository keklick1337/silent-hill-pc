#include "framework/sh_test.h"
#include "formats/common/byte_source.h"
#include "formats/disc/disc_image.h"
#include "formats/iso9660/iso9660.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace fs = std::filesystem;

using sh::disc::Image;
using sh::iso9660::FileInfo;
using sh::iso9660::Volume;

constexpr std::size_t kRaw = 2352;
constexpr std::size_t kBlock = 2048;

fs::path real_bin() {
    return fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
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

struct Cleanup {
    fs::path path;
    ~Cleanup() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// The whole file in one read. Empty optional when the read fails.
std::optional<std::vector<std::uint8_t>> read_all(const sh::ByteSource& source) {
    std::vector<std::uint8_t> bytes(source.size());
    if (!source.read_at(0, bytes.data(), bytes.size())) {
        return std::nullopt;
    }
    return bytes;
}

// Synthetic content: byte i is (i * step + seed) mod 256.
std::uint8_t pattern(std::size_t i, std::size_t step, std::size_t seed) {
    return static_cast<std::uint8_t>(i * step + seed);
}

bool has_pattern(const std::vector<std::uint8_t>& bytes, std::size_t step, std::size_t seed) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] != pattern(i, step, seed)) {
            return false;
        }
    }
    return true;
}

std::optional<Volume> open_real_volume() {
    if (!fs::exists(real_bin())) {
        SH_SKIP("data/Silent Hill (v1.1).bin is missing");
    }
    return Volume::open(Image::open(real_bin()));
}

void expect_file(const Volume& volume, std::string_view path, std::string_view name, std::uint32_t lba,
                 std::uint32_t size) {
    const std::optional<FileInfo> info = volume.find(path);
    SH_CHECK(info.has_value());
    if (!info) {
        return;
    }
    SH_CHECK(info->name == name);
    SH_CHECK(info->lba == lba);
    SH_CHECK(info->size == size);
    SH_CHECK(!info->directory);
}

// Synthetic disc builder. Each block is 2048 bytes of user data; the file below is written as raw sectors.

void put_both_endian32(std::string& s, std::size_t at, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
        const auto byte = static_cast<char>((v >> (8 * i)) & 0xff);
        s[at + i] = byte;      // little-endian copy
        s[at + 7 - i] = byte;  // big-endian copy
    }
}

// One directory record. name is the raw identifier: "\0" for ".", "\1" for "..".
std::string record(const std::string& name, std::uint32_t lba, std::uint32_t size, bool directory) {
    std::size_t length = 33 + name.size();
    if (length % 2 != 0) {
        ++length;
    }
    std::string rec(length, '\0');
    rec[0] = static_cast<char>(length);
    put_both_endian32(rec, 2, lba);
    put_both_endian32(rec, 10, size);
    rec[25] = directory ? '\x02' : '\0';
    rec[32] = static_cast<char>(name.size());
    rec.replace(33, name.size(), name);
    return rec;
}

// Packs records into blocks. A record never crosses a block boundary; the rest of the block stays zero.
std::string pack(const std::vector<std::string>& records) {
    std::string out;
    std::size_t used = 0;
    for (const std::string& rec : records) {
        if (used + rec.size() > kBlock) {
            out.resize(out.size() + (kBlock - used), '\0');
            used = 0;
        }
        out += rec;
        used += rec.size();
    }
    out.resize((out.size() + kBlock - 1) / kBlock * kBlock, '\0');
    return out;
}

// Layout: 16 descriptor, 17-19 root (100 files need three blocks), 20 SUB, 21-23 BIG.BIN, 24 NOTE.TXT.
std::vector<std::string> synthetic_blocks() {
    std::vector<std::string> blocks(25, std::string(kBlock, '\0'));

    std::vector<std::string> root = {
        record(std::string(1, '\0'), 17, 3 * kBlock, true),
        record(std::string(1, '\x01'), 17, 3 * kBlock, true),
        record("BIG.BIN;1", 21, 5000, false),
        record("SUB", 20, kBlock, true),
    };
    for (int i = 0; i < 100; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "FILE_%03d.DAT;1", i);
        root.push_back(record(name, 0, 0, false));
    }
    const std::string root_bytes = pack(root);
    for (std::size_t b = 0; b < 3; ++b) {
        blocks[17 + b] = root_bytes.substr(b * kBlock, kBlock);
    }

    blocks[20] = pack({record(std::string(1, '\0'), 20, kBlock, true),
                       record(std::string(1, '\x01'), 17, 3 * kBlock, true),
                       record("NOTE.TXT;1", 24, 1000, false)});

    std::string big(3 * kBlock, '\0');
    for (std::size_t i = 0; i < 5000; ++i) {
        big[i] = static_cast<char>(pattern(i, 13, 5));
    }
    for (std::size_t b = 0; b < 3; ++b) {
        blocks[21 + b] = big.substr(b * kBlock, kBlock);
    }

    std::string note(kBlock, '\0');
    for (std::size_t i = 0; i < 1000; ++i) {
        note[i] = static_cast<char>(pattern(i, 3, 1));
    }
    blocks[24] = note;

    std::string pvd(kBlock, '\0');
    pvd[0] = 1;
    pvd.replace(1, 5, "CD001");
    pvd[6] = 1;
    pvd[128] = 0x00;  // logical block size 2048, little-endian
    pvd[129] = 0x08;
    pvd[130] = 0x08;  // and big-endian
    pvd[131] = 0x00;
    pvd.replace(156, 34, record(std::string(1, '\0'), 17, 3 * kBlock, true));
    blocks[16] = pvd;
    return blocks;
}

fs::path write_raw_bin(const std::string& name, const std::vector<std::string>& blocks) {
    std::string bytes;
    for (const std::string& block : blocks) {
        std::string raw(kRaw, '\0');
        for (std::size_t i = 1; i <= 10; ++i) {
            raw[i] = '\xff';
        }
        raw[15] = 2;
        raw.replace(24, kBlock, block);
        bytes += raw;
    }
    const fs::path path = scratch(name);
    write_bytes(path, bytes);
    return path;
}

std::optional<Volume> open_synthetic(const fs::path& bin) {
    return Volume::open(Image::open(bin));
}

template <typename Mutate>
void expect_rejected(const char* file, Mutate&& mutate) {
    std::vector<std::string> blocks = synthetic_blocks();
    mutate(blocks[16]);
    const fs::path bin = write_raw_bin(file, blocks);
    const Cleanup cleanup{bin};
    SH_CHECK(Image::open(bin) != nullptr);
    SH_CHECK(!open_synthetic(bin).has_value());
}

}  // namespace

SH_TEST(iso_real_root_lists_the_four_files) {
    const std::optional<Volume> volume = open_real_volume();
    SH_CHECK(volume.has_value());
    if (!volume) {
        return;
    }
    const std::vector<FileInfo> root = volume->list("");
    SH_CHECK(root.size() == 4);
    const std::array<std::string_view, 4> expected = {"SYSTEM.CNF", "SLUS_007.07", "SILENT.", "HILL."};
    for (std::string_view name : expected) {
        const bool found = std::any_of(root.begin(), root.end(), [&](const FileInfo& f) { return f.name == name; });
        SH_CHECK(found);
    }
}

SH_TEST(iso_real_files_have_expected_extents) {
    const std::optional<Volume> volume = open_real_volume();
    SH_CHECK(volume.has_value());
    if (!volume) {
        return;
    }
    const std::optional<FileInfo> root = volume->find("");
    SH_CHECK(root.has_value() && root->directory && root->lba == 22 && root->size == 2048);
    expect_file(*volume, "SYSTEM.CNF", "SYSTEM.CNF", 23, 68);
    expect_file(*volume, "SLUS_007.07", "SLUS_007.07", 24, 81920);
    expect_file(*volume, "SILENT.", "SILENT.", 64, 80476160);
    expect_file(*volume, "HILL.", "HILL.", 39359, 455897088);
}

SH_TEST(iso_real_find_ignores_case_and_rejects_children_of_files) {
    const std::optional<Volume> volume = open_real_volume();
    SH_CHECK(volume.has_value());
    if (!volume) {
        return;
    }
    expect_file(*volume, "silent.", "SILENT.", 64, 80476160);
    expect_file(*volume, "slus_007.07", "SLUS_007.07", 24, 81920);
    expect_file(*volume, "system.cnf", "SYSTEM.CNF", 23, 68);
    SH_CHECK(!volume->find("NO_SUCH_FILE.TXT").has_value());
    SH_CHECK(!volume->find("SYSTEM.CNF/CHILD").has_value());
}

SH_TEST(iso_real_system_cnf_content) {
    const std::optional<Volume> volume = open_real_volume();
    SH_CHECK(volume.has_value());
    if (!volume) {
        return;
    }
    const std::optional<FileInfo> file = volume->find("SYSTEM.CNF");
    if (!file) {
        SH_CHECK(file.has_value());
        return;
    }
    const std::unique_ptr<sh::ByteSource> source = volume->open_file(*file);
    SH_CHECK(source != nullptr);
    if (!source) {
        return;
    }
    SH_CHECK(source->size() == 68);
    const std::optional<std::vector<std::uint8_t>> bytes = read_all(*source);
    SH_CHECK(bytes.has_value());
    if (!bytes) {
        return;
    }
    const std::string text(bytes->begin(), bytes->end());
    SH_CHECK(text == "BOOT = cdrom:\\SLUS_007.07;1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFF00\r\n");
}

SH_TEST(iso_real_slus_partial_reads_match_full_read) {
    const std::optional<Volume> volume = open_real_volume();
    SH_CHECK(volume.has_value());
    if (!volume) {
        return;
    }
    const std::optional<FileInfo> file = volume->find("SLUS_007.07");
    if (!file) {
        SH_CHECK(file.has_value());
        return;
    }
    const std::unique_ptr<sh::ByteSource> source = volume->open_file(*file);
    SH_CHECK(source != nullptr);
    if (!source) {
        return;
    }
    const std::optional<std::vector<std::uint8_t>> full = read_all(*source);
    SH_CHECK(full.has_value());
    if (!full) {
        return;
    }
    SH_CHECK(full->size() == 81920);
    SH_CHECK(std::memcmp(full->data(), "PS-X EXE", 8) == 0);

    struct Window {
        std::uint64_t offset;
        std::size_t count;
    };
    constexpr std::array<Window, 6> windows{{{2040, 16}, {2047, 2}, {2048, 1}, {1000, 5000}, {81919, 1}, {0, 81920}}};
    for (const Window& w : windows) {
        std::vector<std::uint8_t> part(w.count);
        SH_CHECK(source->read_at(w.offset, part.data(), w.count));
        SH_CHECK(std::equal(part.begin(), part.end(), full->begin() + static_cast<std::ptrdiff_t>(w.offset)));
    }

    std::uint8_t buf[4] = {};
    SH_CHECK(source->read_at(81920, buf, 0));
    SH_CHECK(!source->read_at(81920, buf, 1));
    SH_CHECK(!source->read_at(81921, buf, 0));
}

SH_TEST(iso_real_archive_ends_are_bounded) {
    const std::optional<Volume> volume = open_real_volume();
    SH_CHECK(volume.has_value());
    if (!volume) {
        return;
    }
    for (const char* name : {"SILENT.", "HILL."}) {
        const std::optional<FileInfo> file = volume->find(name);
        if (!file) {
            SH_CHECK(file.has_value());
            continue;
        }
        const std::unique_ptr<sh::ByteSource> source = volume->open_file(*file);
        SH_CHECK(source != nullptr);
        if (!source) {
            continue;
        }
        const std::uint64_t size = source->size();
        char tail[16] = {};
        SH_CHECK(source->read_at(size - 16, tail, 16));
        SH_CHECK(!source->read_at(size, tail, 1));
        SH_CHECK(!source->read_at(size - 15, tail, 16));
    }
}

SH_TEST(iso_real_folder_rules) {
    const std::optional<Volume> volume = open_real_volume();
    SH_CHECK(volume.has_value());
    if (!volume) {
        return;
    }
    const std::optional<FileInfo> root = volume->find("");
    SH_CHECK(root.has_value() && volume->open_file(*root) == nullptr);
    SH_CHECK(volume->list("SYSTEM.CNF").empty());
    SH_CHECK(volume->list("NO_SUCH_DIR").empty());
}

SH_TEST(iso_synthetic_nested_folders_and_multi_sector_root) {
    const fs::path bin = write_raw_bin("iso_synthetic.bin", synthetic_blocks());
    const Cleanup cleanup{bin};
    const std::optional<Volume> volume = open_synthetic(bin);
    SH_CHECK(volume.has_value());
    if (!volume) {
        return;
    }

    // BIG.BIN, SUB and 100 FILE_NNN.DAT. "." and ".." are not entries.
    SH_CHECK(volume->list("").size() == 102);
    expect_file(*volume, "file_099.dat", "FILE_099.DAT", 0, 0);
    expect_file(*volume, "big.bin", "BIG.BIN", 21, 5000);
    expect_file(*volume, "sub/Note.Txt", "NOTE.TXT", 24, 1000);

    const std::optional<FileInfo> sub = volume->find("sub");
    SH_CHECK(sub.has_value() && sub->directory);
    if (sub) {
        SH_CHECK(volume->open_file(*sub) == nullptr);
    }
    const std::vector<FileInfo> listing = volume->list("SUB");
    SH_CHECK(listing.size() == 1 && listing[0].name == "NOTE.TXT");
    SH_CHECK(!volume->find("SUB/NOTE.TXT/DEEPER").has_value());
    SH_CHECK(!volume->find("SUB/MISSING.TXT").has_value());

    const std::optional<FileInfo> big = volume->find("BIG.BIN");
    if (!big) {
        SH_CHECK(big.has_value());
        return;
    }
    const std::unique_ptr<sh::ByteSource> source = volume->open_file(*big);
    SH_CHECK(source != nullptr);
    if (!source) {
        return;
    }
    const std::optional<std::vector<std::uint8_t>> full = read_all(*source);
    SH_CHECK(full.has_value());
    if (!full) {
        return;
    }
    SH_CHECK(full->size() == 5000);
    SH_CHECK(has_pattern(*full, 13, 5));

    // Windows that start and end inside sectors, and one that spans the 2048-byte boundary.
    struct Window {
        std::uint64_t offset;
        std::size_t count;
    };
    constexpr std::array<Window, 4> windows{{{2040, 16}, {2047, 2}, {4000, 1000}, {4999, 1}}};
    for (const Window& w : windows) {
        std::vector<std::uint8_t> part(w.count);
        SH_CHECK(source->read_at(w.offset, part.data(), w.count));
        SH_CHECK(std::equal(part.begin(), part.end(), full->begin() + static_cast<std::ptrdiff_t>(w.offset)));
    }
    std::uint8_t byte = 0;
    SH_CHECK(!source->read_at(5000, &byte, 1));

    const std::optional<FileInfo> note = volume->find("SUB/NOTE.TXT");
    const std::unique_ptr<sh::ByteSource> note_source = note ? volume->open_file(*note) : nullptr;
    SH_CHECK(note_source != nullptr);
    if (note_source) {
        const std::optional<std::vector<std::uint8_t>> note_bytes = read_all(*note_source);
        SH_CHECK(note_bytes.has_value() && has_pattern(*note_bytes, 3, 1));
    }
}

SH_TEST(iso_synthetic_rejects_corrupt_directory_record) {
    // The BIG.BIN record starts at offset 68 of the root folder; its name length is at offset 32 of the record.
    std::vector<std::string> blocks = synthetic_blocks();
    blocks[17][68 + 32] = 40;  // longer than the 42-byte record allows
    const fs::path bin = write_raw_bin("iso_corrupt_record.bin", blocks);
    const Cleanup cleanup{bin};
    const std::optional<Volume> volume = open_synthetic(bin);
    SH_CHECK(volume.has_value());
    if (!volume) {
        return;
    }
    SH_CHECK(volume->list("").empty());
    SH_CHECK(!volume->find("FILE_000.DAT").has_value());
}

SH_TEST(iso_synthetic_rejects_bad_volume_descriptor) {
    expect_rejected("iso_bad_type.bin", [](std::string& pvd) { pvd[0] = 2; });
    expect_rejected("iso_bad_identifier.bin", [](std::string& pvd) { pvd[5] = '2'; });
    expect_rejected("iso_bad_block_size.bin", [](std::string& pvd) {
        pvd[128] = 0x00;
        pvd[129] = 0x02;  // 512 bytes
    });
    expect_rejected("iso_root_not_folder.bin", [](std::string& pvd) { pvd[156 + 25] = 0; });

    std::vector<std::string> blocks = synthetic_blocks();
    blocks.resize(16);  // the descriptor sector itself is missing
    const fs::path bin = write_raw_bin("iso_no_descriptor.bin", blocks);
    const Cleanup cleanup{bin};
    SH_CHECK(!open_synthetic(bin).has_value());
}
