#include "formats/convert/filetable/file_table.h"
#include "framework/sh_disc.h"
#include "framework/sh_test.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace {

namespace ft = sh::convert::filetable;
namespace fs = std::filesystem;

fs::path temp_dir(const char* tag) {
    std::random_device rng;
    const fs::path dir = fs::temp_directory_path() /
                         ("sh_filetable_" + std::string(tag) + std::to_string(rng()));
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

std::string replace_first(std::string text, std::string_view from, std::string_view to) {
    const auto at = text.find(from);
    if (at == std::string::npos) {
        return text + " (marker '" + std::string(from) + "' not found)";
    }
    return text.replace(at, from.size(), to);
}

SH_TEST(pack_path_record_round_trip) {
    const ft::Record hero = ft::record_from("CHARA/HERO.ILM", 12345, 67).value();
    SH_CHECK(hero.dir == 3);
    SH_CHECK(hero.type == 7);  // ILM
    SH_CHECK(ft::path_of(hero) == "CHARA/HERO.ILM");
    // w0 = 12345 | 67<<19 = 0x02383039; w1 = 3 | "CHAR"<<4 = 0xCA1A233; w2 = 7<<24.
    const auto bytes = ft::pack(hero);
    const std::uint8_t want[12] = {0x39, 0x30, 0x18, 0x02, 0x83, 0x96, 0xF2, 0x0B, 0x00, 0x00, 0x00, 0x07};
    SH_CHECK(std::memcmp(bytes.data(), want, 12) == 0);

    // A short XA name with no extension: "XA/Z1_16180" -> type 15, spaces pad the name.
    const ft::Record xa = ft::record_from("XA/Z1_16180", 16180, 1).value();
    SH_CHECK(xa.type == 15);
    SH_CHECK(xa.dir == 10);
    SH_CHECK(xa.name4567 != 0);  // "16180" lives in chars 4..7
    SH_CHECK(ft::path_of(xa) == "XA/Z1_16180");
    SH_CHECK(ft::pack(xa)[11] == 0x0F);

    // An 8-char name fills both parts exactly; generic round trips.
    for (const char* path : {"1ST/BODYPROG.BIN", "TIM/001.TIM", "SND/SD_A01E.VAB",
                             "VIN/MAP0_S00.BIN", "MISC/Z1.DAT", "XA/ZZ_14239"}) {
        const auto r = ft::record_from(path, 1, 2);
        SH_CHECK(r);
        if (r) {
            SH_CHECK(ft::path_of(r.value()) == path);
        }
    }
}

SH_TEST(record_from_rejects) {
    SH_CHECK(!ft::record_from("NOPE/FILE.TIM", 0, 0));
    SH_CHECK(!ft::record_from("1ST/FILE.EXT", 0, 0));        // unknown extension
    SH_CHECK(!ft::record_from("1ST/TOOLONGNAME.TIM", 0, 0)); // > 8 chars
    SH_CHECK(!ft::record_from("1ST/NA.ME.TIM", 0, 0));       // '.' inside the name
    SH_CHECK(!ft::record_from("1ST/na`me.TIM", 0, 0));       // outside 0x21..0x5F
    SH_CHECK(!ft::record_from("FILE.TIM", 0, 0));            // no directory
    SH_CHECK(ft::record_from("1ST/BASE.VAB", 100, 5));
}

SH_TEST(from_json_strictness) {
    ft::Table table;
    table.release = "SLUS-00707";
    table.exe_offset = 0xB91C;
    table.files = {ft::record_from("BG/000.TIM", 64, 3).value()};
    const std::string good = ft::to_json(table);

    auto parsed = ft::from_json(good);
    SH_CHECK(parsed);
    if (parsed) {
        SH_CHECK(parsed.value().files.size() == 1);
        SH_CHECK(ft::pack(parsed.value().files[0]) == ft::pack(table.files[0]));
    }

    SH_CHECK(!ft::from_json(replace_first(good, "\"version\": 1", "\"version\": 2")));
    SH_CHECK(!ft::from_json(replace_first(good, "\"sector\": 64,", "")));          // missing field
    SH_CHECK(!ft::from_json(replace_first(good, "\"blocks\": 3", "\"blocks\": 4096"))); // out of range
    SH_CHECK(!ft::from_json(replace_first(good, "BG/000.TIM", "NOWHERE/X.TIM")));  // bad path
}

SH_TEST(read_from_exe_rejects) {
    std::vector<std::uint8_t> exe(0x20000, 0);
    SH_CHECK(!ft::read_from_exe(exe));                                        // unknown CRC
    SH_CHECK(!ft::read_from_exe(std::span<const std::uint8_t>(exe.data(), 100)));  // too short
}

SH_TEST(load_or_import_requires_data) {
    const fs::path dir = temp_dir("empty");
    auto result = ft::load_or_import(dir);
    SH_CHECK(!result);
    if (!result) {
        SH_CHECK(result.error().find("filetable.json") != std::string::npos);
        SH_CHECK(result.error().find("SLUS_007.07") != std::string::npos);
        SH_CHECK(result.error().find("--reimport") != std::string::npos);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

SH_TEST(real_image_table) {
    auto disc = sh::test::open_game_disc();
    if (!disc) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    // The raw exe bytes, opened the sh_disc.h way: image -> volume -> SLUS_007.07.
    const auto volume = sh::iso9660::Volume::open(disc->image);
    SH_CHECK(volume.has_value());
    const auto slus = volume->find("SLUS_007.07");
    SH_CHECK(slus.has_value());
    const auto exe_source = volume->open_file(*slus);
    std::vector<std::uint8_t> exe(static_cast<std::size_t>(exe_source->size()));
    SH_CHECK(exe_source->read_at(0, exe.data(), exe.size()));

    auto table = ft::read_from_exe(exe);
    SH_CHECK(table);
    if (!table) return;
    SH_CHECK(table.value().release == "SLUS-00707");
    SH_CHECK(table.value().exe_offset == 0xB91C);
    SH_CHECK(table.value().files.size() == 2074);
    SH_CHECK(disc->table.size() == 2074);

    for (std::size_t i = 0; i < table.value().files.size(); ++i) {
        const auto packed = ft::pack(table.value().files[i]);
        if (std::memcmp(packed.data(), exe.data() + 0xB91C + i * 12, 12) != 0) {
            SH_CHECK(false);
            return;
        }
        if (ft::path_of(table.value().files[i]) != disc->table[i].name) {
            SH_CHECK(false);
            return;
        }
    }

    // The JSON round trip keeps every packed byte.
    auto reparsed = ft::from_json(ft::to_json(table.value()));
    SH_CHECK(reparsed);
    if (!reparsed) return;
    for (std::size_t i = 0; i < table.value().files.size(); ++i) {
        if (ft::pack(reparsed.value().files[i]) != ft::pack(table.value().files[i])) {
            SH_CHECK(false);
            return;
        }
    }

    // load_or_import builds the JSON from raw/ (verifying its own repack), then reads it back.
    const fs::path dir = temp_dir("import");
    fs::create_directories(dir / "raw");
    std::ofstream(dir / "raw" / "SLUS_007.07", std::ios::binary)
        .write(reinterpret_cast<const char*>(exe.data()), static_cast<std::streamsize>(exe.size()));
    auto loaded = ft::load_or_import(dir);
    SH_CHECK(loaded);
    SH_CHECK(fs::exists(dir / ft::kRelativePath));
    if (loaded) {
        SH_CHECK(loaded.value().files.size() == 2074);
        auto again = ft::load_or_import(dir);
        SH_CHECK(again);
        if (again) {
            SH_CHECK(ft::pack(again.value().files[0]) == ft::pack(loaded.value().files[0]));
        }
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace
