#include "framework/sh_test.h"

#include "engine/vfs/disc_sectors.h"
#include "engine/vfs/game_files_layer.h"
#include "engine/vfs/vfs.h"
#include "formats/convert/gamefiles/game_files.h"
#include "formats/disc/disc_image.h"
#include "import/import.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using sh::vfs::DiscSectors;
using sh::vfs::SectorKind;
using sh::vfs::Vfs;

namespace {

using Bytes = std::optional<std::vector<std::uint8_t>>;

const fs::path kImage = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";

void write_text(const fs::path& file, std::string_view content) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << content;
}

fs::path scratch_dir(const std::string& name) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "vfs" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

// raw/ of the real image, imported once (no converters, about two seconds) and kept between runs.
// SH_TEST_GAMEDATA names an existing import instead.
fs::path test_gamedata() {
    if (const char* env = std::getenv("SH_TEST_GAMEDATA"); env && env[0]) {
        return env;
    }
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "vfs" / "gamedata";
    if (fs::exists(dir / "version.cfg")) {
        return dir;
    }
    if (!fs::exists(kImage)) {
        return {};
    }
    sh::import::Request request;
    request.image = kImage;
    request.gamedata_dir = dir;
    request.overwrite = true;
    request.verify = false;
    const std::vector<std::unique_ptr<sh::convert::Converter>> none;
    if (!sh::import::import_image(request, none, nullptr)) {
        return {};
    }
    return dir;
}

// The image the imported files are compared with. SH_TEST_GAMEDATA can name an import on a machine that has no
// image in data/: there is nothing to compare with then.
std::shared_ptr<sh::disc::Image> image_or_skip() {
    std::shared_ptr<sh::disc::Image> image = sh::disc::Image::open(kImage);
    if (!image) {
        SH_SKIP("no disc image in data/ to compare the game data with");
    }
    return image;
}

}  // namespace

SH_TEST(vfs_gamedata_layer_needs_a_finished_import) {
    const fs::path dir = scratch_dir("gamedata_unfinished");
    write_text(dir / "index.tsv", "path\tsize\tcrc32\tsource\tformat\tconverted\nSYSTEM.CNF\t3\t0\tISO:file\tcnf\t\n");
    write_text(dir / "raw" / "SYSTEM.CNF", "abc");
    std::string error;
    SH_CHECK(sh::vfs::make_gamedata_layer(dir, error) == nullptr);
    SH_CHECK(!error.empty());

    write_text(dir / "version.cfg", "importer = 1\ngame = SLES-123.45\n");
    error.clear();
    SH_CHECK(sh::vfs::make_gamedata_layer(dir, error) == nullptr);
    SH_CHECK(error.find("SLES-123.45") != std::string::npos);
}

SH_TEST(vfs_gamedata_layer_uses_index_sizes_and_keeps_tails) {
    const fs::path dir = scratch_dir("gamedata_small");
    write_text(dir / "version.cfg", "importer = 1\ngame = SLUS-007.07\n");
    write_text(dir / "index.tsv",
               "path\tsize\tcrc32\tsource\tformat\tconverted\n"
               "CHARA/A.TIM\t3\t0\tSILENT:id=1\ttim\t\n"
               "SND/B.VAB\t4\t0\tSILENT:id=2\tvab\t\n");
    write_text(dir / "raw" / "CHARA" / "A.TIM", "abcTAIL");
    write_text(dir / "raw" / "SND" / "B.VAB", "ab");  // shorter than its logical size: damaged
    std::string error;
    const std::unique_ptr<sh::vfs::Layer> layer = sh::vfs::make_gamedata_layer(dir, error);
    SH_CHECK(layer != nullptr);
    if (!layer) {
        return;
    }
    SH_CHECK(layer->exists("chara\\a.tim"));
    const Bytes a = layer->read("CHARA/A.TIM");
    SH_CHECK(a && std::string(a->begin(), a->end()) == "abc");
    const std::unique_ptr<sh::ByteSource> opened = layer->open("CHARA/A.TIM");
    SH_CHECK(opened && opened->size() == 3);
    const std::unique_ptr<sh::ByteSource> stored = layer->open_stored("CHARA/A.TIM");
    SH_CHECK(stored && stored->size() == 7);
    SH_CHECK(!layer->read("SND/B.VAB").has_value());
    SH_CHECK((layer->list("") == std::vector<std::string>{"CHARA/", "SND/"}));
    SH_CHECK((layer->list("chara") == std::vector<std::string>{"A.TIM"}));
}

SH_TEST(vfs_list_and_open_merge_layers) {
    const fs::path low = scratch_dir("list_low");
    const fs::path high = scratch_dir("list_high");
    write_text(low / "CHARA" / "HERO.ILM", "low");
    write_text(low / "CHARA" / "AR.ILM", "low");
    write_text(high / "chara" / "hero.ilm", "high!");
    write_text(high / "XA" / "NEW", "x");
    Vfs vfs;
    vfs.add_layer(sh::vfs::make_directory_layer(low, "low"), 0);
    vfs.add_layer(sh::vfs::make_directory_layer(high, "high"), 10);
    SH_CHECK((vfs.list("") == std::vector<std::string>{"CHARA/", "XA/"}));
    SH_CHECK((vfs.list("CHARA/") == std::vector<std::string>{"AR.ILM", "HERO.ILM"}));
    const std::unique_ptr<sh::ByteSource> hero = vfs.open("CHARA/HERO.ILM");
    SH_CHECK(hero && hero->size() == 5);
    SH_CHECK(vfs.find("chara/ar.ilm") != nullptr && vfs.find("chara/ar.ilm")->name() == "low");
    SH_CHECK(!vfs.open("CHARA/NONE").get());
    SH_CHECK(!vfs.find("CHARA/HERO.ILM")->host_path("CHARA/HERO.ILM").empty());
}

// Every file of the import reads through the VFS as the same bytes as through the disc image.
SH_TEST(vfs_gamedata_files_match_image) {
    const fs::path gamedata = test_gamedata();
    if (gamedata.empty()) {
        SH_SKIP("no disc image in data/ and no SH_TEST_GAMEDATA");
    }
    const std::shared_ptr<sh::disc::Image> image = image_or_skip();
    std::string error;
    const std::vector<sh::vfs::GameDataFile> rows = sh::vfs::read_gamedata_index(gamedata, error);
    SH_CHECK(rows.size() == 2076);

    Vfs from_gamedata;
    from_gamedata.add_layer(sh::vfs::make_gamedata_layer(gamedata, error), 0);
    Vfs from_image;
    from_image.add_layer(sh::vfs::make_disc_layer(image), 0);

    std::size_t same = 0;
    std::uint64_t bytes = 0;
    for (const sh::vfs::GameDataFile& row : rows) {
        const Bytes a = from_gamedata.read(row.path);
        const Bytes b = from_image.read(row.path);
        if (a && b && *a == *b && a->size() == row.size) {
            ++same;
            bytes += a->size();
        } else {
            std::printf("  differs: %s\n", row.path.c_str());
        }
    }
    std::printf("  %zu of %zu files equal, %llu bytes\n", same, rows.size(), static_cast<unsigned long long>(bytes));
    SH_CHECK(same == rows.size());
}

// Every sector of every file in the LBA map reads as the image's sector: SILENT. as Form 1 data (tails included),
// HILL. both as 2336-byte XA sectors and as Form 1 data.
SH_TEST(vfs_gamedata_sectors_match_image) {
    const fs::path gamedata = test_gamedata();
    if (gamedata.empty()) {
        SH_SKIP("no disc image in data/ and no SH_TEST_GAMEDATA");
    }
    const std::shared_ptr<sh::disc::Image> image = image_or_skip();
    std::string error;
    Vfs vfs;
    vfs.add_layer(sh::vfs::make_gamedata_layer(gamedata, error), 0);
    const std::unique_ptr<DiscSectors> sectors = DiscSectors::open(vfs, gamedata, error);
    SH_CHECK(sectors != nullptr);
    if (!sectors) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    SH_CHECK(sectors->runs().size() == 2074 - 7);  // the seven empty entries hold no sectors
    SH_CHECK(sectors->sector_count() == 39359 + 222606);

    std::array<std::uint8_t, sh::vfs::kXaSectorBytes> got{};
    std::array<std::uint8_t, sh::vfs::kXaSectorBytes> want{};
    std::uint64_t data_same = 0, xa_same = 0, differ = 0, covered = 0;
    for (const sh::vfs::SectorRun& run : sectors->runs()) {
        for (std::uint32_t i = 0; i < run.sectors; ++i) {
            const std::uint32_t lba = run.lba + i;
            ++covered;
            for (const SectorKind kind : {SectorKind::Data, SectorKind::Xa}) {
                if (kind == SectorKind::Xa && !run.hill) {
                    SH_CHECK(!sectors->read(lba, kind, got.data()));
                    continue;
                }
                const bool ok = sectors->read(lba, kind, got.data());
                sh::vfs::read_image_sector(*image, lba, kind, want.data());
                if (ok && std::equal(got.begin(), got.begin() + sh::vfs::sector_bytes(kind), want.begin())) {
                    ++(kind == SectorKind::Xa ? xa_same : data_same);
                } else if (++differ <= 5) {
                    std::printf("  lba %u (%s) differs\n", lba, run.path.c_str());
                }
            }
        }
    }
    std::printf("  %llu sectors in files; equal: data %llu, xa %llu; differ %llu\n",
                static_cast<unsigned long long>(covered), static_cast<unsigned long long>(data_same),
                static_cast<unsigned long long>(xa_same), static_cast<unsigned long long>(differ));
    SH_CHECK(differ == 0);
    // The ISO 9660 area and SLUS_007.07 are not addressed by the game and are not in the map.
    SH_CHECK(!sectors->read(16, SectorKind::Data, got.data()));
    SH_CHECK(!sectors->read(sectors->sector_count(), SectorKind::Data, got.data()));

    const sh::vfs::SectorRun* xa = sectors->find("\\XA\\05_02152;1");
    SH_CHECK(xa != nullptr && xa->lba == 39359 && xa->sectors == 2152 && xa->hill);
    SH_CHECK(sectors->run_at(39359) == xa);
}

// The game files store (formats/convert/gamefiles/game_files.h) gives every sector the game reads as the image has
// it, tails included, with no raw/ layer: the files the engine never reads by sector (XA voice and music tracks)
// are the only ones left out. With SH_TEST_GAMEDATA naming a full import the converters' rebuilds are checked too.
SH_TEST(vfs_game_files_sectors_match_image) {
    const fs::path gamedata = test_gamedata();
    if (gamedata.empty()) {
        SH_SKIP("no disc image in data/ and no SH_TEST_GAMEDATA");
    }
    const std::shared_ptr<sh::disc::Image> image = image_or_skip();
    if (!sh::convert::gamefiles::has_raw(gamedata)) {
        SH_SKIP("the test game data has no raw/ to build the store from");
    }
    // The store copies the kept files (about 300 MB): it is removed again unless the game data had one.
    const bool had_store = fs::exists(gamedata / sh::convert::gamefiles::kManifest);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            if (!dir.empty()) {
                std::error_code ec;
                fs::remove_all(dir, ec);
                sh::convert::gamefiles::forget(dir.parent_path().parent_path());
            }
        }
    } cleanup{had_store ? fs::path() : gamedata / sh::convert::gamefiles::kStoreDir};
    const auto built = sh::convert::gamefiles::build_store(gamedata, false, 0);
    SH_CHECK(static_cast<bool>(built));
    if (!built) {
        std::printf("  %s\n", built.error().c_str());
        return;
    }
    std::printf("  store: %s\n", sh::convert::gamefiles::format_build_line(built.value()).c_str());
    for (std::size_t i = 0; i < built.value().notes.size() && i < 5; ++i) {
        std::printf("  %s\n", built.value().notes[i].c_str());
    }
    SH_CHECK(built.value().notes.empty());

    std::string error;
    Vfs vfs;
    vfs.add_layer(sh::vfs::make_game_files_layer(gamedata, nullptr, error), 0);
    const std::unique_ptr<DiscSectors> sectors = DiscSectors::open(vfs, gamedata, error);
    SH_CHECK(sectors != nullptr);
    if (!sectors) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    const auto store = sh::convert::gamefiles::store_for(gamedata);
    SH_CHECK(store != nullptr);
    if (!store) {
        return;
    }
    std::array<std::uint8_t, sh::vfs::kXaSectorBytes> got{};
    std::array<std::uint8_t, sh::vfs::kXaSectorBytes> want{};
    std::uint64_t same = 0, differ = 0, left_out = 0;
    for (const sh::vfs::SectorRun& run : sectors->runs()) {
        const bool served = store->find(run.path) != nullptr;
        if (!served) {
            SH_CHECK(run.path.starts_with("XA/"));
            ++left_out;
            continue;
        }
        for (std::uint32_t i = 0; i < run.sectors; ++i) {
            for (const SectorKind kind : {SectorKind::Data, SectorKind::Xa}) {
                if (kind == SectorKind::Xa && !run.hill) {
                    continue;
                }
                const bool ok = sectors->read(run.lba + i, kind, got.data());
                sh::vfs::read_image_sector(*image, run.lba + i, kind, want.data());
                if (ok && std::equal(got.begin(), got.begin() + sh::vfs::sector_bytes(kind), want.begin())) {
                    ++same;
                } else if (++differ <= 5) {
                    std::printf("  lba %u (%s) differs\n", run.lba + i, run.path.c_str());
                }
            }
        }
    }
    std::printf("  %llu sectors equal, %llu differ, %llu files left out\n", static_cast<unsigned long long>(same),
                static_cast<unsigned long long>(differ), static_cast<unsigned long long>(left_out));
    SH_CHECK(differ == 0);
    // The XA voice and music tracks when their WAVs were converted; SYSTEM.CNF and the executable are not in the map.
    SH_CHECK(left_out + 2 == built.value().skipped);
}

// A mod that replaces a file replaces its sectors; what the mod file lacks reads as zeros.
SH_TEST(vfs_gamedata_mod_replaces_sectors) {
    const fs::path gamedata = test_gamedata();
    if (gamedata.empty()) {
        SH_SKIP("no disc image in data/ and no SH_TEST_GAMEDATA");
    }
    const fs::path mod = scratch_dir("sector_mod");
    std::string content(3000, 'M');
    write_text(mod / "chara" / "hero.tim", content);

    std::string error;
    Vfs vfs;
    vfs.add_layer(sh::vfs::make_gamedata_layer(gamedata, error), INT32_MIN);
    vfs.add_layer(sh::vfs::make_directory_layer(mod, "mod"), 0);
    const std::unique_ptr<DiscSectors> sectors = DiscSectors::open(vfs, gamedata, error);
    SH_CHECK(sectors != nullptr);
    if (!sectors) {
        return;
    }
    const sh::vfs::SectorRun* hero = sectors->find("CHARA/HERO.TIM");
    SH_CHECK(hero != nullptr && hero->sectors >= 2);
    if (!hero) {
        return;
    }
    std::array<std::uint8_t, sh::vfs::kDataSectorBytes> got{};
    SH_CHECK(sectors->read(hero->lba, SectorKind::Data, got.data()));
    SH_CHECK(got[0] == 'M' && got[2047] == 'M');
    SH_CHECK(sectors->read(hero->lba + 1, SectorKind::Data, got.data()));
    SH_CHECK(got[3000 - 2048 - 1] == 'M' && got[3000 - 2048] == 0 && got[2047] == 0);
}
