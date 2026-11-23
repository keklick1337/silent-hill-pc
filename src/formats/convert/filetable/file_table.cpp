#include "formats/convert/filetable/file_table.h"

#include "formats/common/crc32.h"
#include "formats/common/json.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <system_error>
#include <utility>

namespace sh::convert::filetable {

namespace fs = std::filesystem;

namespace {

constexpr std::array<const char*, 11> kDirectories = {
    "1ST", "ANIM", "BG", "CHARA", "ITEM", "MISC", "SND", "TEST", "TIM", "VIN", "XA"};
constexpr std::array<const char*, 12> kExtensions = {
    "TIM", "VAB", "BIN", "DMS", "ANM", "PLM", "IPD", "ILM", "TMD", "DAT", "KDT", "CMP"};

constexpr std::size_t kReleaseCheckBytes = 4096;
constexpr std::uint32_t kSectorLimit = 1u << 19;
constexpr std::uint32_t kBlockLimit = 1u << 12;
constexpr int kNameChars = 8;
constexpr int kVersion = 1;

struct Release {
    std::uint32_t crc32;
    const char* serial;
    const char* label;
    std::uint32_t offset;
    std::uint32_t count;
};

// The executable the game is built for.
constexpr std::array<Release, 1> kReleases = {{
    {0xCF9CD8E5u, "SLUS-00707", "NTSC 1.1", 0xB91C, 2074},
}};

std::uint32_t load_le32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
           (std::uint32_t{p[3]} << 24);
}

void store_le32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

// Character k of the name as stored (ASCII - 0x20; 0 is a space and ends the name).
std::uint32_t name_char(const Record& r, int k) {
    const std::uint32_t part = k < 4 ? r.name0123 : r.name4567;
    return (part >> (6 * (k % 4))) & 63;
}

std::string name_of(const Record& r) {
    std::string name;
    for (int k = 0; k < kNameChars; ++k) {
        const std::uint32_t c = name_char(r, k);
        if (c == 0) {
            break;
        }
        name += static_cast<char>(c + 0x20);
    }
    return name;
}

// The game decodes names up to the first space and looks them up up to the first '.', so a name must be one run
// of printable characters without a dot, padded with spaces.
Result<void> check_record(const Record& r) {
    if (r.sector >= kSectorLimit || r.blocks >= kBlockLimit) {
        return Result<void>::fail("sector or size out of range");
    }
    if (r.dir >= kDirectories.size()) {
        return Result<void>::fail("directory index " + std::to_string(r.dir) + " is not known");
    }
    if (r.type >= kExtensions.size() && r.type != kNoType) {
        return Result<void>::fail("type " + std::to_string(r.type) + " is not known");
    }
    if (r.name0123 >= (1u << 24) || r.name4567 >= (1u << 24)) {
        return Result<void>::fail("name part out of range");
    }
    bool ended = false;
    for (int k = 0; k < kNameChars; ++k) {
        const std::uint32_t c = name_char(r, k);
        if (c == 0) {
            ended = true;
        } else if (ended) {
            return Result<void>::fail("a character follows the end of the name");
        } else if (c + 0x20 == '.') {
            return Result<void>::fail("'.' inside the name");
        }
    }
    if (name_char(r, 0) == 0) {
        return Result<void>::fail("empty name");
    }
    return Result<void>::ok();
}

Result<std::vector<std::uint8_t>> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<std::vector<std::uint8_t>>::fail("cannot open " + path.string());
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return Result<std::vector<std::uint8_t>>::fail("cannot read " + path.string());
    }
    return Result<std::vector<std::uint8_t>>::ok(std::move(bytes));
}

Result<void> write_atomic(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        return Result<void>::fail("cannot create " + path.parent_path().string() + ": " + ec.message());
    }
    // Parallel game starts (replay, map sweep) can build the file at once: each writes its own temporary.
    fs::path tmp = path;
    tmp += ".tmp-" + std::to_string(std::random_device{}());
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return Result<void>::fail("cannot write " + tmp.string());
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return Result<void>::fail("cannot rename " + tmp.string() + ": " + ec.message());
    }
    return Result<void>::ok();
}

// The table from raw/SLUS_007.07 and its JSON text, after checking that the text reads back to the very bytes of
// the executable: the file is the only copy of the table the game has.
Result<std::pair<Table, std::string>> build_from_raw(const fs::path& gamedata_dir) {
    using R = Result<std::pair<Table, std::string>>;
    const fs::path exe_path = gamedata_dir / kRawExe;
    const auto exe = read_bytes(exe_path);
    if (!exe) {
        return R::fail(exe.error());
    }
    auto table = read_from_exe(exe.value());
    if (!table) {
        return R::fail(exe_path.string() + ": " + table.error());
    }
    std::string text = to_json(table.value());
    const auto back = from_json(text);
    if (!back) {
        return R::fail("the written table does not read back: " + back.error());
    }
    const Table& t = back.value();
    if (t.files.size() != table.value().files.size() || t.exe_offset != table.value().exe_offset) {
        return R::fail("the written table does not read back: entry count or offset differs");
    }
    for (std::size_t i = 0; i < t.files.size(); ++i) {
        const auto bytes = pack(t.files[i]);
        if (std::memcmp(bytes.data(), exe.value().data() + t.exe_offset + i * kRecordBytes, kRecordBytes) != 0) {
            return R::fail("the written table differs from the executable at entry " + std::to_string(i));
        }
    }
    return R::ok({std::move(table.value()), std::move(text)});
}

std::string where(std::size_t index, const json::Value& v) {
    return "files[" + std::to_string(index) + "] (line " + std::to_string(v.line()) + ")";
}

}  // namespace

std::span<const char* const> directories() { return kDirectories; }
std::span<const char* const> extensions() { return kExtensions; }

Result<Table> read_from_exe(std::span<const std::uint8_t> exe) {
    if (exe.size() < kReleaseCheckBytes) {
        return Result<Table>::fail("the executable is too short");
    }
    const std::uint32_t crc = sh::crc32(exe.data(), kReleaseCheckBytes);
    const Release* release = nullptr;
    for (const Release& r : kReleases) {
        if (r.crc32 == crc) {
            release = &r;
        }
    }
    if (release == nullptr) {
        char text[16];
        std::snprintf(text, sizeof text, "%08X", crc);
        return Result<Table>::fail(std::string("unrecognised executable (CRC32 of the first 4096 bytes ") + text + ")");
    }
    if (exe.size() < std::size_t{release->offset} + std::size_t{release->count} * kRecordBytes) {
        return Result<Table>::fail("the executable is too short for its file table");
    }

    Table table;
    table.release = release->serial;
    table.exe_offset = release->offset;
    table.files.reserve(release->count);
    for (std::uint32_t i = 0; i < release->count; ++i) {
        const std::uint8_t* p = exe.data() + release->offset + std::size_t{i} * kRecordBytes;
        const std::uint32_t w0 = load_le32(p);
        const std::uint32_t w1 = load_le32(p + 4);
        const std::uint32_t w2 = load_le32(p + 8);
        if ((w0 >> 31) != 0 || (w1 >> 28) != 0 || (w2 >> 28) != 0) {
            return Result<Table>::fail("entry " + std::to_string(i) + ": reserved bits are set");
        }
        Record r;
        r.sector = w0 & (kSectorLimit - 1);
        r.blocks = (w0 >> 19) & (kBlockLimit - 1);
        r.dir = static_cast<std::uint8_t>(w1 & 15);
        r.name0123 = (w1 >> 4) & 0xFFFFFF;
        r.name4567 = w2 & 0xFFFFFF;
        r.type = static_cast<std::uint8_t>((w2 >> 24) & 15);
        if (const auto ok = check_record(r); !ok) {
            return Result<Table>::fail("entry " + std::to_string(i) + ": " + ok.error());
        }
        table.files.push_back(r);
    }
    return Result<Table>::ok(std::move(table));
}

std::array<std::uint8_t, kRecordBytes> pack(const Record& r) {
    std::array<std::uint8_t, kRecordBytes> out{};
    store_le32(out.data(), (r.sector & (kSectorLimit - 1)) | ((r.blocks & (kBlockLimit - 1)) << 19));
    store_le32(out.data() + 4, (std::uint32_t{r.dir} & 15) | ((r.name0123 & 0xFFFFFF) << 4));
    store_le32(out.data() + 8, (r.name4567 & 0xFFFFFF) | ((std::uint32_t{r.type} & 15) << 24));
    return out;
}

std::string path_of(const Record& r) {
    std::string path = r.dir < kDirectories.size() ? kDirectories[r.dir] : "?";
    path += '/';
    path += name_of(r);
    if (r.type < kExtensions.size()) {
        path += '.';
        path += kExtensions[r.type];
    }
    return path;
}

Result<Record> record_from(std::string_view path, std::uint32_t sector, std::uint32_t blocks) {
    const std::string text(path);
    const std::size_t slash = path.find('/');
    if (slash == std::string_view::npos) {
        return Result<Record>::fail("\"" + text + "\": no directory");
    }
    Record r;
    r.sector = sector;
    r.blocks = blocks;
    const std::string_view dir = path.substr(0, slash);
    std::size_t d = 0;
    while (d < kDirectories.size() && dir != kDirectories[d]) {
        ++d;
    }
    if (d == kDirectories.size()) {
        return Result<Record>::fail("\"" + text + "\": unknown directory");
    }
    r.dir = static_cast<std::uint8_t>(d);

    std::string_view name = path.substr(slash + 1);
    r.type = kNoType;
    if (const std::size_t dot = name.rfind('.'); dot != std::string_view::npos) {
        const std::string_view ext = name.substr(dot + 1);
        std::size_t t = 0;
        while (t < kExtensions.size() && ext != kExtensions[t]) {
            ++t;
        }
        if (t == kExtensions.size()) {
            return Result<Record>::fail("\"" + text + "\": unknown extension");
        }
        r.type = static_cast<std::uint8_t>(t);
        name = name.substr(0, dot);
    }
    if (name.empty() || name.size() > static_cast<std::size_t>(kNameChars)) {
        return Result<Record>::fail("\"" + text + "\": the name must have 1 to 8 characters");
    }
    for (std::size_t k = 0; k < name.size(); ++k) {
        const auto c = static_cast<unsigned char>(name[k]);
        if (c <= 0x20 || c > 0x5F || c == '.') {
            return Result<Record>::fail("\"" + text + "\": character not allowed in a name");
        }
        const std::uint32_t v = std::uint32_t{c} - 0x20;
        if (k < 4) {
            r.name0123 |= v << (6 * k);
        } else {
            r.name4567 |= v << (6 * (k - 4));
        }
    }
    if (sector >= kSectorLimit || blocks >= kBlockLimit) {
        return Result<Record>::fail("\"" + text + "\": sector or size out of range");
    }
    return Result<Record>::ok(r);
}

std::string to_json(const Table& table) {
    json::Writer w(true);
    w.begin_object();
    w.member("version", kVersion);
    w.member("source", "SLUS_007.07");
    w.member("release", table.release);
    w.member("exe_offset", table.exe_offset);
    w.key("files").begin_array();
    for (const Record& r : table.files) {
        w.inline_next().begin_object();
        w.member("path", path_of(r));
        w.member("sector", r.sector);
        w.member("blocks", r.blocks);
        w.end_object();
    }
    w.end_array();
    w.end_object();
    std::string text = w.take();
    text += '\n';
    return text;
}

Result<Table> from_json(std::string_view text) {
    const auto parsed = json::parse(text);
    if (!parsed) {
        return Result<Table>::fail(parsed.error());
    }
    const json::Value& root = parsed.value();
    if (!root.is_object()) {
        return Result<Table>::fail("the document is not an object");
    }
    const json::Value* version = root.find("version");
    if (version == nullptr || !version->is_integer() || version->integer() != kVersion) {
        return Result<Table>::fail("\"version\" must be 1");
    }
    const json::Value* source = root.find("source");
    const json::Value* release = root.find("release");
    const json::Value* offset = root.find("exe_offset");
    const json::Value* files = root.find("files");
    if (source == nullptr || !source->is_string()) {
        return Result<Table>::fail("\"source\" must be a string");
    }
    if (release == nullptr || !release->is_string()) {
        return Result<Table>::fail("\"release\" must be a string");
    }
    if (offset == nullptr || !offset->is_integer() || offset->integer() < 0 || offset->integer() > 0xFFFFFFFF) {
        return Result<Table>::fail("\"exe_offset\" must be a non-negative integer");
    }
    if (files == nullptr || !files->is_array()) {
        return Result<Table>::fail("\"files\" must be an array");
    }

    Table table;
    table.release = release->string();
    table.exe_offset = static_cast<std::uint32_t>(offset->integer());
    table.files.reserve(files->array().size());
    for (std::size_t i = 0; i < files->array().size(); ++i) {
        const json::Value& e = files->array()[i];
        if (!e.is_object()) {
            return Result<Table>::fail(where(i, e) + ": not an object");
        }
        const json::Value* path = e.find("path");
        const json::Value* sector = e.find("sector");
        const json::Value* blocks = e.find("blocks");
        if (path == nullptr || !path->is_string()) {
            return Result<Table>::fail(where(i, e) + ": \"path\" must be a string");
        }
        if (sector == nullptr || !sector->is_integer() || sector->integer() < 0 || sector->integer() >= kSectorLimit) {
            return Result<Table>::fail(where(i, e) + ": \"sector\" must be an integer in 0..524287");
        }
        if (blocks == nullptr || !blocks->is_integer() || blocks->integer() < 0 || blocks->integer() >= kBlockLimit) {
            return Result<Table>::fail(where(i, e) + ": \"blocks\" must be an integer in 0..4095");
        }
        const auto record = record_from(path->string(), static_cast<std::uint32_t>(sector->integer()),
                                        static_cast<std::uint32_t>(blocks->integer()));
        if (!record) {
            return Result<Table>::fail(where(i, e) + ": " + record.error());
        }
        table.files.push_back(record.value());
    }
    return Result<Table>::ok(std::move(table));
}

Result<bool> write_from_raw(const fs::path& gamedata_dir, bool overwrite) {
    const fs::path path = gamedata_dir / kRelativePath;
    std::error_code ec;
    if (!overwrite && fs::exists(path, ec)) {
        return Result<bool>::ok(false);
    }
    auto built = build_from_raw(gamedata_dir);
    if (!built) {
        return Result<bool>::fail(built.error());
    }
    if (const auto written = write_atomic(path, built.value().second); !written) {
        return Result<bool>::fail(written.error());
    }
    return Result<bool>::ok(true);
}

Result<Table> load_or_import(const fs::path& gamedata_dir) {
    const fs::path path = gamedata_dir / kRelativePath;
    std::error_code ec;
    if (fs::exists(path, ec)) {
        const auto bytes = read_bytes(path);
        if (!bytes) {
            return Result<Table>::fail(bytes.error());
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size());
        auto table = from_json(text);
        if (!table) {
            return Result<Table>::fail(path.string() + ": " + table.error());
        }
        return table;
    }
    if (!fs::exists(gamedata_dir / kRawExe, ec)) {
        return Result<Table>::fail("neither " + path.string() + " nor " + (gamedata_dir / kRawExe).string() +
                                   " exists: import the disc image again (--reimport)");
    }
    auto built = build_from_raw(gamedata_dir);
    if (!built) {
        return Result<Table>::fail(built.error());
    }
    if (const auto written = write_atomic(path, built.value().second); !written) {
        return Result<Table>::fail(written.error());
    }
    return Result<Table>::ok(std::move(built.value().first));
}

}  // namespace sh::convert::filetable
