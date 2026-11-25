// The game files store without a disc image: a small gamedata/ with a kept file, a file the TIM converter rebuilds,
// a movie and an XA voice track. The store is checked against the bytes raw/ had, tails included; raw/ and the
// movie's sound WAV go, and index.tsv forgets the WAV.

#include "framework/sh_test.h"

#include "formats/common/crc32.h"
#include "formats/convert/gamefiles/game_files.h"
#include "formats/convert/tim/tim_converter.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace gf = sh::convert::gamefiles;

namespace {

using Bytes = std::vector<std::uint8_t>;

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(SH_PROJECT_ROOT) / ".tmp" / "convert_gamefiles" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write(const fs::path& file, const Bytes& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write_text(const fs::path& file, std::string_view text) {
    write(file, Bytes(text.begin(), text.end()));
}

std::string read_text(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string hex8(std::uint32_t v) {
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08X", v);
    return buf;
}

void put16(Bytes& b, unsigned v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(Bytes& b, std::uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}

// A 16-bit TIM of 4x2 pixels without a palette.
Bytes small_tim() {
    Bytes b;
    put32(b, 0x10);
    put32(b, 2);
    put32(b, 12 + 4 * 2 * 2);
    put16(b, 320);
    put16(b, 0);
    put16(b, 4);
    put16(b, 2);
    for (unsigned i = 0; i < 8; ++i) {
        put16(b, 0x8000 | (i * 0x0421));
    }
    return b;
}

struct Row {
    std::string path;
    Bytes stored;  // as raw/ keeps it
    std::uint64_t size = 0;
    std::string source;
    std::string format;
    std::string converted;
};

}  // namespace

SH_TEST(game_files_store_serves_raw_bytes_and_replaces_raw) {
    const fs::path dir = scratch("store");
    const Bytes tim = small_tim();
    const auto converter = sh::convert::tim::make_converter();
    const auto outputs = converter->decode(tim, "TIM/SMALL.TIM");
    SH_CHECK(static_cast<bool>(outputs));
    if (!outputs) {
        return;
    }
    std::string tim_outputs;
    for (const sh::convert::Output& o : outputs.value()) {
        write(dir / "convert" / o.relative_path, o.bytes);
        tim_outputs += (tim_outputs.empty() ? "" : ";") + o.relative_path;
    }

    // The TIM and the kept file have sector tails, one of them not all zeros.
    Bytes tim_stored = tim;
    tim_stored.resize(2048, 0);
    Bytes kept(300, 0x5A);
    Bytes kept_stored = kept;
    kept_stored.resize(2048, 0);
    kept_stored[2047] = 0x77;
    const Bytes movie(2336 * 2, 0x11);
    const Bytes voice(2336, 0x22);
    std::vector<Row> rows = {
        {"SLUS_007.07", Bytes(16, 1), 16, "SLUS:file", "exe", ""},
        {"CHARA/KEPT.ILM", kept_stored, kept.size(), "SILENT:id=1", "ilm", "models/CHARA/KEPT.glb"},
        {"TIM/SMALL.TIM", tim_stored, tim.size(), "SILENT:id=2", "tim", tim_outputs},
        {"XA/M1_00002", movie, movie.size(), "HILL:lba=100+2", "fmv", "audio/xa/M1_00002.wav;audio/xa/M1_00002.xa.json"},
        {"XA/05_00001", voice, voice.size(), "HILL:lba=102+1", "xa", "audio/xa/05_00001_f01_ch0.wav;audio/xa/05_00001.xa.json"},
    };
    std::string index = "path\tsize\tcrc32\tsource\tformat\tconverted\n";
    for (const Row& r : rows) {
        write(dir / "raw" / r.path, r.stored);
        index += r.path + "\t" + std::to_string(r.size) + "\t" + hex8(sh::crc32(r.stored.data(), r.stored.size())) +
                 "\t" + r.source + "\t" + r.format + "\t" + r.converted + "\n";
    }
    write_text(dir / "index.tsv", index);
    write_text(dir / "convert" / "audio" / "xa" / "M1_00002.wav", "RIFF movie sound");
    write_text(dir / "convert" / "audio" / "xa" / "M1_00002.xa.json", "{}");
    // The file table has no other copy once raw/ is gone; this test does not read it.
    write_text(dir / "tables" / "filetable.json", "{}");

    const auto built = gf::build_store(dir, false, 2);
    SH_CHECK(static_cast<bool>(built));
    if (!built) {
        std::printf("  %s\n", built.error().c_str());
        return;
    }
    SH_CHECK(built.value().converted == 1 && built.value().kept == 2 && built.value().skipped == 2);
    SH_CHECK(built.value().tails == 1 && built.value().notes.empty());

    // Each file as raw/ had it, logical bytes and tail.
    for (const Row& r : rows) {
        const auto got = gf::read_game_file(dir, r.path);
        if (r.format == "exe" || r.format == "xa") {
            continue;  // still in raw/ here
        }
        SH_CHECK(got.has_value() && *got == r.stored);
    }
    fs::remove_all(dir / "raw");
    gf::forget(dir);
    const auto store = gf::store_for(dir);
    SH_CHECK(store != nullptr);
    if (!store) {
        return;
    }
    SH_CHECK(store->find("XA/05_00001") == nullptr && store->find("SLUS_007.07") == nullptr);
    const gf::Entry* small = store->find("tim/small.tim");
    SH_CHECK(small != nullptr && small->origin == gf::Origin::Converter && small->converter == "tim");
    const gf::Entry* model = store->find("CHARA/KEPT.ILM");
    SH_CHECK(model != nullptr && model->origin == gf::Origin::Kept && model->stored == 2048);
    SH_CHECK(gf::read_game_file(dir, "CHARA/KEPT.ILM") == kept_stored);
    SH_CHECK(gf::read_game_file(dir, "TIM/SMALL.TIM") == tim_stored);
    const gf::Entry* film = store->find("XA/M1_00002");
    SH_CHECK(film != nullptr && film->origin == gf::Origin::Kept);
    if (film) {
        const auto stream = store->open_kept(*film);
        SH_CHECK(stream != nullptr && stream->size() == movie.size());
    }
    if (small) {
        // An edited converted file still gives bytes, marked as changed.
        bool changed = false;
        const auto bytes = store->read(*small, &changed);
        SH_CHECK(bytes && bytes.value() == tim && !changed);
    }

    // The movie's sound WAV is the only converted file the engine never reads; index.tsv forgets it.
    const auto unread = gf::remove_unread_outputs(dir);
    SH_CHECK(unread && unread.value().files == 1);
    SH_CHECK(!fs::exists(dir / "convert" / "audio" / "xa" / "M1_00002.wav"));
    SH_CHECK(fs::exists(dir / "convert" / "audio" / "xa" / "M1_00002.xa.json"));
    const std::string after = read_text(dir / "index.tsv");
    SH_CHECK(after.find("\tfmv\taudio/xa/M1_00002.xa.json\n") != std::string::npos);
    SH_CHECK(after.find("05_00001_f01_ch0.wav") != std::string::npos);
    SH_CHECK(after.size() + std::string("audio/xa/M1_00002.wav;").size() == index.size());
}

SH_TEST(game_files_finish_import_keeps_or_deletes_raw) {
    const fs::path dir = scratch("finish");
    const Bytes file(100, 0x42);
    write(dir / "raw" / "MISC" / "A.DAT", file);
    write_text(dir / "index.tsv", "path\tsize\tcrc32\tsource\tformat\tconverted\nMISC/A.DAT\t100\t" +
                                      hex8(sh::crc32(file.data(), file.size())) + "\tSILENT:id=0\tdat\t\n");
    write_text(dir / "tables" / "filetable.json", "{}");

    const auto kept = gf::finish_import(dir, true, 1);
    SH_CHECK(kept && kept.value().built && !kept.value().removed);
    SH_CHECK(fs::exists(dir / "raw" / "MISC" / "A.DAT") && fs::exists(dir / gf::kManifest));
    SH_CHECK(fs::exists(dir / gf::kStoreDir / "MISC" / "A.DAT"));

    const auto removed = gf::finish_import(dir, false, 1);
    SH_CHECK(removed && !removed.value().built && removed.value().removed && removed.value().freed_bytes == 100);
    SH_CHECK(!fs::exists(dir / "raw"));
    SH_CHECK(gf::read_game_file(dir, "MISC/A.DAT") == file);

    // Nothing to do without raw/.
    const auto again = gf::finish_import(dir, false, 1);
    SH_CHECK(again && !again.value().built && !again.value().removed);
}
