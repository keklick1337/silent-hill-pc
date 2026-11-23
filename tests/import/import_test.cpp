#include "framework/sh_test.h"
#include "formats/archive/sh_archive.h"
#include "formats/common/byte_source.h"
#include "formats/common/crc32.h"
#include "formats/convert/converter.h"
#include "formats/disc/disc_image.h"
#include "formats/iso9660/iso9660.h"
#include "import/import.h"
#include "import/index.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

// Imports the real disc image into .tmp/import-core/ and checks the result against the image itself. Each test that
// needs the image is skipped when data/ has none. The full imports read and write about 600 MB.

namespace {

namespace fs = std::filesystem;
namespace imp = sh::import;
using Bytes = std::vector<std::uint8_t>;

fs::path project_root() {
    return fs::path(SH_PROJECT_ROOT);
}

fs::path image_bin() {
    return project_root() / "data" / "Silent Hill (v1.1).bin";
}

fs::path image_cue() {
    return project_root() / "data" / "Silent Hill (v1.1).cue";
}

fs::path require_image() {
    if (!fs::exists(image_bin()) || !fs::exists(image_cue())) {
        SH_SKIP("data/Silent Hill (v1.1).bin/.cue is not present");
    }
    return image_cue();
}

// A fresh directory under .tmp/import-core/, deleted when the test ends.
class WorkDir {
public:
    explicit WorkDir(const std::string& name) : path_(project_root() / ".tmp" / "import-core" / ("test-" + name)) {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }
    ~WorkDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    WorkDir(const WorkDir&) = delete;
    WorkDir& operator=(const WorkDir&) = delete;

    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

Bytes read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string read_text(const fs::path& path) {
    const Bytes bytes = read_all(path);
    return std::string(bytes.begin(), bytes.end());
}

void write_all(const fs::path& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::string> names_in(const fs::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

// ---- fake converters ----------------------------------------------------------------------------------------------

enum class Mode { Copy, Summary, DecodeFails, WrongRoundTrip };

class FakeConverter final : public sh::convert::Converter {
public:
    FakeConverter(std::string name, Mode mode, std::string prefix, std::string suffix)
        : name_(std::move(name)), mode_(mode), prefix_(std::move(prefix)), suffix_(std::move(suffix)) {}

    std::string_view name() const override { return name_; }
    bool lossless() const override { return mode_ != Mode::Summary; }

    bool accepts(std::span<const std::uint8_t>, std::string_view path) const override {
        return path.starts_with(prefix_) && path.ends_with(suffix_);
    }

    sh::Result<std::vector<sh::convert::Output>> decode(std::span<const std::uint8_t> raw,
                                                        std::string_view path) const override {
        using R = sh::Result<std::vector<sh::convert::Output>>;
        if (mode_ == Mode::DecodeFails) {
            return R::fail("injected decode failure");
        }
        sh::convert::Output out;
        if (mode_ == Mode::Summary) {
            const std::string text = "size=" + std::to_string(raw.size());
            out.relative_path = "fake/" + std::string(path) + ".txt";
            out.bytes.assign(text.begin(), text.end());
        } else {
            out.relative_path = "fake/" + std::string(path) + ".copy";
            out.bytes.assign(raw.begin(), raw.end());
        }
        std::vector<sh::convert::Output> outputs;
        outputs.push_back(std::move(out));
        return R::ok(std::move(outputs));
    }

    sh::Result<Bytes> encode(std::span<const sh::convert::Output> outputs) const override {
        Bytes bytes = outputs[0].bytes;
        if (mode_ == Mode::WrongRoundTrip && !bytes.empty()) {
            bytes[0] ^= 0xFF;
        }
        return sh::Result<Bytes>::ok(std::move(bytes));
    }

private:
    std::string name_;
    Mode mode_;
    std::string prefix_;
    std::string suffix_;
};

std::vector<std::unique_ptr<sh::convert::Converter>> good_converters() {
    std::vector<std::unique_ptr<sh::convert::Converter>> list;
    list.push_back(std::make_unique<FakeConverter>("copy", Mode::Copy, "1ST/", ".TIM"));
    list.push_back(std::make_unique<FakeConverter>("summary", Mode::Summary, "CHARA/", ".ILM"));
    return list;
}

// ---- reference data from the image --------------------------------------------------------------------------------

struct Reference {
    std::shared_ptr<const sh::disc::Image> image;
    std::vector<sh::archive::Entry> entries;
};

Reference open_reference() {
    Reference ref;
    ref.image.reset(sh::disc::Image::open(image_bin()).release());
    const auto volume = sh::iso9660::Volume::open(ref.image);
    SH_CHECK(volume.has_value());
    if (!volume) {
        return ref;
    }
    const auto slus = volume->find("SLUS_007.07");
    const auto silent = volume->find("SILENT.");
    const auto hill = volume->find("HILL.");
    SH_CHECK(slus && silent && hill);
    if (!slus || !silent || !hill) {
        return ref;
    }
    const std::unique_ptr<sh::ByteSource> exe = volume->open_file(*slus);
    const auto sectors = [](std::uint32_t bytes) { return (bytes + 2047) / 2048; };
    auto table = sh::archive::read_file_table(*exe, {silent->lba, sectors(silent->size)}, {hill->lba, sectors(hill->size)});
    SH_CHECK(table.has_value());
    if (table) {
        ref.entries = std::move(*table);
    }
    return ref;
}

// The bytes the image holds for an entry, sector by sector: 2048 per sector for SILENT., 2336 for HILL.
Bytes sectors_from_image(const Reference& ref, const sh::archive::Entry& e) {
    Bytes bytes;
    if (e.region == sh::archive::Region::Silent) {
        bytes.resize(std::size_t{e.sectors} * 2048);
        for (std::uint32_t s = 0; s < e.sectors; ++s) {
            SH_CHECK(ref.image->read_user_data(e.lba + s, bytes.data() + std::size_t{s} * 2048));
        }
    } else {
        bytes.resize(std::size_t{e.sectors} * 2336);
        Bytes raw(2352);
        for (std::uint32_t s = 0; s < e.sectors; ++s) {
            SH_CHECK(ref.image->read_raw(e.lba + s, raw.data()));
            std::copy(raw.begin() + 16, raw.end(), bytes.begin() + std::ptrdiff_t{s} * 2336);
        }
    }
    return bytes;
}

struct Recorder {
    std::vector<std::string> stages;  // distinct, in order
    double last = 0;
    bool monotonic = true;
    bool in_range = true;

    imp::ProgressFn fn() {
        return [this](std::string_view stage, double fraction) {
            if (stages.empty() || stages.back() != stage) {
                stages.emplace_back(stage);
            }
            monotonic = monotonic && fraction >= last;
            in_range = in_range && fraction >= 0.0 && fraction <= 1.0;
            last = fraction;
        };
    }
};

}  // namespace

// ---- refusals (cheap) ---------------------------------------------------------------------------------------------

SH_TEST(import_refuses_missing_and_unsupported_input) {
    WorkDir work("refuse");
    const auto converters = std::vector<std::unique_ptr<sh::convert::Converter>>();

    imp::Request req;
    req.gamedata_dir = work.path() / "gamedata";
    req.image = work.path() / "nothing.bin";
    auto res = imp::import_image(req, converters, nullptr);
    SH_CHECK(!res);
    SH_CHECK(contains(res.error(), "not found"));

    // A single game file (a PS-X EXE) is not an image.
    Bytes exe(4096, 0);
    std::copy_n("PS-X EXE", 8, exe.begin());
    write_all(work.path() / "SLUS_007.07", exe);
    req.image = work.path() / "SLUS_007.07";
    res = imp::import_image(req, converters, nullptr);
    SH_CHECK(!res);
    SH_CHECK(contains(res.error(), "single game file"));

    // A .bin that is no multiple of 2352.
    write_all(work.path() / "odd.bin", Bytes(5000, 0x11));
    req.image = work.path() / "odd.bin";
    res = imp::import_image(req, converters, nullptr);
    SH_CHECK(!res);
    SH_CHECK(contains(res.error(), "2352"));

    // Another extension.
    write_all(work.path() / "BODYPROG.BIN.dat", Bytes(5000, 0x22));
    req.image = work.path() / "BODYPROG.BIN.dat";
    res = imp::import_image(req, converters, nullptr);
    SH_CHECK(!res);
    SH_CHECK(contains(res.error(), "unsupported input"));

    // Nothing was created, not even a temporary directory.
    SH_CHECK(!fs::exists(req.gamedata_dir));
    for (const std::string& name : names_in(work.path())) {
        SH_CHECK(name == "SLUS_007.07" || name == "odd.bin" || name == "BODYPROG.BIN.dat");
    }
}

// A non-empty directory without version.cfg (an old interrupted import, or something else) is not cleaned silently.
SH_TEST(import_refuses_incomplete_existing_directory_without_overwrite) {
    WorkDir work("incomplete");
    const fs::path target = work.path() / "gamedata";
    fs::create_directories(target / "raw");
    write_all(target / "raw" / "SYSTEM.CNF", Bytes{1, 2, 3});

    imp::Request req;
    req.image = image_cue();
    req.gamedata_dir = target;
    const auto converters = std::vector<std::unique_ptr<sh::convert::Converter>>();
    const auto res = imp::import_image(req, converters, nullptr);
    SH_CHECK(!res);
    SH_CHECK(contains(res.error(), "import_overwrite"));
    SH_CHECK(fs::exists(target / "raw" / "SYSTEM.CNF"));
    SH_CHECK((names_in(work.path()) == std::vector<std::string>{"gamedata"}));
}

SH_TEST(import_refuses_plain_2048_iso_and_opens_raw_iso) {
    const fs::path cue = require_image();
    WorkDir work("iso");
    const std::shared_ptr<const sh::disc::Image> image(sh::disc::Image::open(cue).release());
    SH_CHECK(image != nullptr);
    if (!image) {
        return;
    }

    // The first 70 sectors of the disc: volume descriptor, root, SYSTEM.CNF and SLUS_007.07 (LBA 24..63) are inside.
    constexpr std::uint32_t kSectors = 70;
    Bytes cooked;
    Bytes raw;
    for (std::uint32_t lba = 0; lba < kSectors; ++lba) {
        Bytes sector(2352);
        SH_CHECK(image->read_raw(lba, sector.data()));
        raw.insert(raw.end(), sector.begin(), sector.end());
        cooked.insert(cooked.end(), sector.begin() + 24, sector.begin() + 24 + 2048);
    }
    write_all(work.path() / "cooked.iso", cooked);
    write_all(work.path() / "short-raw.iso", raw);

    const auto converters = std::vector<std::unique_ptr<sh::convert::Converter>>();
    imp::Request req;
    req.gamedata_dir = work.path() / "gamedata";

    req.image = work.path() / "cooked.iso";
    auto res = imp::import_image(req, converters, nullptr);
    SH_CHECK(!res);
    SH_CHECK(contains(res.error(), "2048 bytes per sector"));
    SH_CHECK(contains(res.error(), ".cue or .bin"));

    // A raw 2352 .iso is accepted as an image; this cut of the disc fails later, on the layout of SILENT. and HILL.
    req.image = work.path() / "short-raw.iso";
    res = imp::import_image(req, converters, nullptr);
    SH_CHECK(!res);
    SH_CHECK(contains(res.error(), "layout"));
    SH_CHECK(!contains(res.error(), "unsupported input"));

    SH_CHECK(!fs::exists(req.gamedata_dir));
    const std::vector<std::string> left = names_in(work.path());
    SH_CHECK((left == std::vector<std::string>{"cooked.iso", "short-raw.iso"}));
}

// ---- injected conversion errors (a full raw pass) -----------------------------------------------------------------

SH_TEST(import_conversion_errors_fail_the_import_and_keep_the_target_intact) {
    const fs::path cue = require_image();
    WorkDir work("failure");

    // An existing directory with a marker: a failed import with overwrite must leave it alone.
    const fs::path target = work.path() / "gamedata";
    fs::create_directories(target);
    write_all(target / "marker.txt", Bytes{1, 2, 3});

    std::vector<std::unique_ptr<sh::convert::Converter>> converters;
    converters.push_back(std::make_unique<FakeConverter>("broken-decode", Mode::DecodeFails, "1ST/", "FONT16.TIM"));
    converters.push_back(std::make_unique<FakeConverter>("broken-roundtrip", Mode::WrongRoundTrip, "1ST/", "KONAMI.TIM"));

    imp::Report report;
    imp::Request req;
    req.image = cue;
    req.gamedata_dir = target;
    req.overwrite = true;
    req.threads = 3;
    req.report = &report;
    const auto res = imp::import_image(req, converters, nullptr);
    SH_CHECK(!res);
    SH_CHECK(contains(res.error(), "1ST/FONT16.TIM"));
    SH_CHECK(contains(res.error(), "injected decode failure"));
    SH_CHECK(contains(res.error(), "1ST/KONAMI.TIM"));
    SH_CHECK(contains(res.error(), "round trip"));

    SH_CHECK(report.failures.size() == 2);
    SH_CHECK(report.converters.size() == 2);

    SH_CHECK(!fs::exists(target / "version.cfg"));
    SH_CHECK(!fs::exists(target / "index.tsv"));
    SH_CHECK(fs::exists(target / "marker.txt"));
    SH_CHECK((names_in(work.path()) == std::vector<std::string>{"gamedata"}));
}

// ---- the full import ----------------------------------------------------------------------------------------------

SH_TEST(import_real_image_full_import_is_complete_and_repeatable) {
    const fs::path cue = require_image();
    WorkDir work("full");
    const Reference ref = open_reference();
    SH_CHECK(ref.entries.size() == 2074);

    const fs::path target = work.path() / "gamedata";
    const auto converters = good_converters();
    imp::Report report;
    Recorder recorder;
    imp::Request req;
    req.image = cue;
    req.gamedata_dir = target;
    req.threads = 4;
    req.report = &report;
    auto res = imp::import_image(req, converters, recorder.fn());
    SH_CHECK(res);
    if (!res) {
        std::printf("    import error: %s\n", res.error().c_str());
        return;
    }

    SH_CHECK(recorder.monotonic);
    SH_CHECK(recorder.in_range);
    SH_CHECK(recorder.last == 1.0);
    SH_CHECK((recorder.stages == std::vector<std::string>{"read", "raw", "convert", "verify", "commit"}));
    SH_CHECK((names_in(work.path()) == std::vector<std::string>{"gamedata"}));
    SH_CHECK((names_in(target) == std::vector<std::string>{"convert", "index.tsv", "raw", "version.cfg"}));

    const std::string version = read_text(target / "version.cfg");
    SH_CHECK(contains(version, "importer = 13\n"));
    SH_CHECK(contains(version, "image_sha256 = cf975c962d8ecd7b57ed43127fb5ce41a693c713ae4917c14e79dabde5250794\n"));
    SH_CHECK(contains(version, "image_name = Silent Hill (v1.1).cue\n"));
    SH_CHECK(contains(version, "game = SLUS-007.07\n"));
    SH_CHECK(contains(version, "converters = copy,summary\n"));

    const std::string index_text = read_text(target / "index.tsv");
    const auto parsed = imp::parse_index(index_text);
    SH_CHECK(parsed);
    if (!parsed) {
        return;
    }
    const std::vector<imp::IndexRow>& rows = parsed.value();
    SH_CHECK(rows.size() == 2076);
    SH_CHECK(rows.size() == report.raw_files);
    SH_CHECK(report.crc_checked == rows.size());
    SH_CHECK(report.image_sha256 == "cf975c962d8ecd7b57ed43127fb5ce41a693c713ae4917c14e79dabde5250794");
    if (rows.size() != 2076) {
        return;
    }
    SH_CHECK(rows[0].path == "SYSTEM.CNF" && rows[1].path == "SLUS_007.07");

    // Known values from docs/FORMATS.md, section 5 (computed from the image).
    const std::map<std::string, std::pair<std::uint64_t, std::uint32_t>> known = {
        {"CHARA/HERO.ILM", {14592, 0x2D30905C}},
        {"1ST/BODYPROG.BIN", {674560, 0xAC9CDBC1}},
        {"VIN/MAPT_S00.BIN", {0, 0x00000000}},
        {"XA/05_02152", {5027072, 0xDED3232A}},
        {"SLUS_007.07", {81920, 0x1C2B2258}},
        {"SYSTEM.CNF", {68, 0x1F1A87CC}},
    };
    std::size_t known_seen = 0;

    std::uint64_t stored_sum = 0;
    std::uint64_t copy_accepted = 0;
    std::uint64_t summary_accepted = 0;
    std::set<std::string> paths;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const imp::IndexRow& row = rows[i];
        paths.insert(row.path);
        const fs::path file = target / "raw" / fs::path(row.path);
        const Bytes data = read_all(file);
        stored_sum += data.size();

        // CRC32 of every file against the index.
        SH_CHECK(sh::crc32(data.data(), data.size()) == row.crc32);

        if (i >= 2) {
            const sh::archive::Entry& e = ref.entries[i - 2];
            SH_CHECK(e.name == row.path);
            SH_CHECK(row.size == e.size);
            // The stored bytes are the sectors of the image, tail included.
            SH_CHECK(data == sectors_from_image(ref, e));
            if (e.region == sh::archive::Region::Silent) {
                SH_CHECK(row.source == "SILENT:id=" + std::to_string(e.id));
                SH_CHECK(data.size() == std::size_t{e.sectors} * 2048 && data.size() >= row.size);
            } else {
                SH_CHECK(row.source == "HILL:lba=" + std::to_string(e.lba) + "+" + std::to_string(e.sectors));
                SH_CHECK(data.size() == row.size && data.size() % 2336 == 0);
                SH_CHECK(row.format == (e.id < 2053 ? "xa" : "fmv"));
                SH_CHECK(row.path.starts_with("XA/"));
            }
        }

        const auto it = known.find(row.path);
        if (it != known.end()) {
            ++known_seen;
            SH_CHECK(row.size == it->second.first && row.crc32 == it->second.second);
        }

        // Converted files are on disk and carry what the fake converters were given: the logical bytes.
        for (const std::string& converted : row.converted) {
            const Bytes out = read_all(target / "convert" / fs::path(converted));
            SH_CHECK(!out.empty());
            if (converted.ends_with(".copy")) {
                SH_CHECK(out.size() == row.size);
                SH_CHECK(std::equal(out.begin(), out.end(), data.begin()));
            } else {
                const std::string text = "size=" + std::to_string(row.size);
                SH_CHECK(std::string(out.begin(), out.end()) == text);
            }
        }
        if (row.path.starts_with("1ST/") && row.path.ends_with(".TIM")) {
            ++copy_accepted;
            SH_CHECK(row.converted.size() == 1);
        } else if (row.path.starts_with("CHARA/") && row.path.ends_with(".ILM")) {
            ++summary_accepted;
            SH_CHECK(row.converted.size() == 1);
        } else {
            SH_CHECK(row.converted.empty());
        }
    }
    SH_CHECK(known_seen == known.size());
    SH_CHECK(paths.size() == rows.size());
    SH_CHECK(stored_sum == 600565764);  // docs/FORMATS.md, section 6
    SH_CHECK(report.raw_bytes == stored_sum);

    SH_CHECK(copy_accepted > 0 && summary_accepted > 0);
    SH_CHECK(report.converters.size() == 2);
    SH_CHECK(report.converters[0].name == "copy" && report.converters[0].lossless);
    SH_CHECK(report.converters[0].accepted == copy_accepted && report.converters[0].round_trips == copy_accepted);
    SH_CHECK(report.converters[1].name == "summary" && !report.converters[1].lossless);
    SH_CHECK(report.converters[1].accepted == summary_accepted && report.converters[1].round_trips == 0);
    SH_CHECK(report.converted_files == copy_accepted + summary_accepted);
    SH_CHECK(report.failures.empty());

    // The directory exists and overwrite is off: refused at once, nothing changes.
    imp::Request again = req;
    again.report = nullptr;
    res = imp::import_image(again, converters, nullptr);
    SH_CHECK(!res);
    SH_CHECK(contains(res.error(), "already exists"));
    SH_CHECK(read_text(target / "index.tsv") == index_text);
    SH_CHECK((names_in(work.path()) == std::vector<std::string>{"gamedata"}));

    // Replace with another thread count, from the .bin, without verification: the same index.tsv, byte for byte.
    again.overwrite = true;
    again.threads = 1;
    again.verify = false;
    again.image = image_bin();
    res = imp::import_image(again, converters, nullptr);
    SH_CHECK(res);
    if (!res) {
        std::printf("    second import error: %s\n", res.error().c_str());
    }
    SH_CHECK(read_text(target / "index.tsv") == index_text);
    const std::string second_version = read_text(target / "version.cfg");
    SH_CHECK(contains(second_version, "image_name = Silent Hill (v1.1).bin\n"));
    SH_CHECK(contains(second_version, "verify = 0\n"));
    SH_CHECK((names_in(work.path()) == std::vector<std::string>{"gamedata"}));
}

// Without converters the import still gives raw/, index.tsv and version.cfg, and no convert/.
SH_TEST(import_real_image_without_converters_gives_raw_only) {
    const fs::path cue = require_image();
    WorkDir work("raw-only");
    imp::Request req;
    req.image = cue;
    req.gamedata_dir = work.path() / "gamedata";
    req.verify = false;
    const std::vector<std::unique_ptr<sh::convert::Converter>> none;
    const auto res = imp::import_image(req, none, nullptr);
    SH_CHECK(res);
    if (!res) {
        std::printf("    import error: %s\n", res.error().c_str());
        return;
    }
    SH_CHECK((names_in(req.gamedata_dir) == std::vector<std::string>{"index.tsv", "raw", "version.cfg"}));
    SH_CHECK(contains(read_text(req.gamedata_dir / "version.cfg"), "converters = none\n"));
    SH_CHECK(fs::file_size(req.gamedata_dir / "raw" / "SLUS_007.07") == 81920);
}

// ---- cancel -------------------------------------------------------------------------------------------------------

namespace {

// Sets the token from the progress callback when `stage` shows a fraction above `after_fraction` of its own.
void expect_cancel_from_callback(const std::string& name, std::string_view stage) {
    const fs::path cue = require_image();
    WorkDir work(name);
    const fs::path target = work.path() / "gamedata";
    const auto converters = good_converters();

    std::atomic<bool> cancel{false};
    bool seen_stage = false;
    bool same_thread = true;
    const std::thread::id caller = std::this_thread::get_id();
    imp::Request req;
    req.image = cue;
    req.gamedata_dir = target;
    req.threads = 2;
    req.cancel = &cancel;
    const auto res = imp::import_image(req, converters, [&](std::string_view s, double fraction) {
        same_thread = same_thread && std::this_thread::get_id() == caller;
        if (s == stage && (fraction > 0.0 || s != "read")) {
            seen_stage = true;
            cancel.store(true);
        }
    });
    SH_CHECK(seen_stage);
    SH_CHECK(same_thread);
    SH_CHECK(!res);
    SH_CHECK(res.error() == "cancelled");
    SH_CHECK(!fs::exists(target));
    SH_CHECK(names_in(work.path()).empty());
}

}  // namespace

SH_TEST(import_cancel_from_progress_callback_in_read_stage) {
    expect_cancel_from_callback("cancel-read", "read");
}

SH_TEST(import_cancel_from_progress_callback_in_convert_stage) {
    expect_cancel_from_callback("cancel-convert", "convert");
}

SH_TEST(import_cancel_from_progress_callback_in_verify_stage) {
    expect_cancel_from_callback("cancel-verify", "verify");
}

// Another thread cancels while the image is being read; an existing target stays as it was.
SH_TEST(import_cancel_from_another_thread_keeps_existing_target) {
    const fs::path cue = require_image();
    WorkDir work("cancel-thread");
    const fs::path target = work.path() / "gamedata";
    fs::create_directories(target);
    write_all(target / "marker.txt", Bytes{9});

    std::atomic<bool> cancel{false};
    std::thread canceller([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        cancel.store(true);
    });
    imp::Request req;
    req.image = cue;
    req.gamedata_dir = target;
    req.overwrite = true;
    req.cancel = &cancel;
    const auto converters = good_converters();
    const auto res = imp::import_image(req, converters, nullptr);
    canceller.join();
    SH_CHECK(!res);
    SH_CHECK(res.error() == "cancelled");
    SH_CHECK(fs::exists(target / "marker.txt"));
    SH_CHECK(!fs::exists(target / "version.cfg"));
    SH_CHECK((names_in(work.path()) == std::vector<std::string>{"gamedata"}));
}
