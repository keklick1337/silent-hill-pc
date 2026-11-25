#include "formats/convert/gamefiles/game_files.h"

#include "formats/common/crc32.h"
#include "formats/convert/anim/anim_converter.h"
#include "formats/convert/archive/archive_converters.h"
#include "formats/convert/audio/audio_converter.h"
#include "formats/convert/converter.h"
#include "formats/convert/cutscene/cutscene_converter.h"
#include "formats/convert/filetable/file_table.h"
#include "formats/convert/level/map_unified.h"
#include "formats/convert/text/text_converter.h"
#include "formats/convert/tim/tim_converter.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>

namespace sh::convert::gamefiles {

namespace fs = std::filesystem;

namespace {

constexpr char kTailsMagic[8] = {'S', 'H', 'T', 'A', 'I', 'L', 'S', '1'};
constexpr std::string_view kManifestHeader = "path\tsize\tstored\tcrc32\tsource\toutputs";

// CRC-32 of the logical bytes followed by the sector tail, as index.tsv has it.
std::uint32_t stored_crc(std::vector<std::uint8_t> bytes, const std::vector<std::uint8_t>& tail) {
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    return sh::crc32(bytes.data(), bytes.size());
}

// The lossless converters the store rebuilds files with, and the folders of convert/ their outputs live in.
struct ConverterKind {
    const char* name;
    std::vector<std::string_view> prefixes;
};

const std::vector<ConverterKind>& converter_kinds() {
    static const std::vector<ConverterKind> kinds = {
        {"tim", {"textures/", "indexed/"}},
        {"text", {"text/"}},
        {"kdt", {"audio/seq/"}},
        {"cutscene", {"cutscenes/"}},
        {"cmp", {"unpacked/"}},
        {"demo", {"demo/"}},
        {"anim", {"anim/"}},
    };
    return kinds;
}

std::unique_ptr<Converter> make_lossless(std::string_view name) {
    if (name == "tim") return tim::make_converter();
    if (name == "text") return text::make_converter();
    if (name == "kdt") return audio::make_kdt_converter();
    if (name == "cutscene") return cutscene::make_converter();
    if (name == "cmp") return archive::make_cmp_converter();
    if (name == "demo") return archive::make_demo_converter();
    // The sibling file only matters for decode(); encode() reads the JSON alone.
    if (name == "anim") return anim::make_converter(nullptr);
    return nullptr;
}

std::string normalize(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (char c : path) {
        c = c == '\\' ? '/' : c;
        out += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
    }
    while (!out.empty() && out.front() == '/') {
        out.erase(out.begin());
    }
    return out;
}

std::vector<std::string_view> split(std::string_view text, char sep) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(sep, start);
        parts.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            return parts;
        }
        start = end + 1;
    }
}

std::optional<std::vector<std::uint8_t>> read_whole(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::optional<std::vector<std::uint8_t>> read_range(const fs::path& path, std::uint64_t offset, std::uint64_t count) {
    std::ifstream in(path, std::ios::binary);
    if (!in || !in.seekg(static_cast<std::streamoff>(offset))) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(count));
    if (count != 0 && !in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(count))) {
        return std::nullopt;
    }
    return out;
}

Result<void> write_atomic(const fs::path& path, const void* data, std::size_t size) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || (size != 0 && !out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size)))) {
            return Result<void>::fail("cannot write " + tmp.string());
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return Result<void>::fail("cannot rename " + tmp.string() + " to " + path.string());
    }
    return Result<void>::ok();
}

std::string hex8(std::uint32_t v) {
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08X", v);
    return buf;
}

bool parse_u64(std::string_view s, std::uint64_t& out) {
    if (s.empty()) {
        return false;
    }
    out = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
        out = out * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return true;
}

bool parse_hex32(std::string_view s, std::uint32_t& out) {
    if (s.empty() || s.size() > 8) {
        return false;
    }
    out = 0;
    for (const char c : s) {
        const int d = c >= '0' && c <= '9'   ? c - '0'
                      : c >= 'A' && c <= 'F' ? c - 'A' + 10
                      : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                             : -1;
        if (d < 0) {
            return false;
        }
        out = (out << 4) | static_cast<std::uint32_t>(d);
    }
    return true;
}

std::uint64_t tree_bytes(const fs::path& dir) {
    std::uint64_t total = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code size_ec;
        if (it->is_regular_file(size_ec)) {
            total += it->file_size(size_ec);
        }
    }
    return total;
}

// The bytes of a ByteSource window: the first `size` bytes of `inner`.
class WindowSource final : public ByteSource {
public:
    WindowSource(std::unique_ptr<ByteSource> inner, std::uint64_t size) : inner_(std::move(inner)), size_(size) {}
    std::uint64_t size() const override { return size_; }
    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        return offset <= size_ && count <= size_ - offset && inner_->read_at(offset, dst, count);
    }

private:
    std::unique_ptr<ByteSource> inner_;
    std::uint64_t size_;
};

// ---- sector tails file: magic, u32 count, then per file: u16 path length, path, u32 tail length, tail ----------

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

std::vector<std::uint8_t> pack_tails(const std::map<std::string, std::vector<std::uint8_t>>& tails) {
    std::vector<std::uint8_t> out(kTailsMagic, kTailsMagic + sizeof kTailsMagic);
    put_u32(out, static_cast<std::uint32_t>(tails.size()));
    for (const auto& [path, bytes] : tails) {
        out.push_back(static_cast<std::uint8_t>(path.size()));
        out.push_back(static_cast<std::uint8_t>(path.size() >> 8));
        out.insert(out.end(), path.begin(), path.end());
        put_u32(out, static_cast<std::uint32_t>(bytes.size()));
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

bool unpack_tails(const std::vector<std::uint8_t>& in,
                  std::unordered_map<std::string, std::vector<std::uint8_t>>& out) {
    std::size_t pos = 0;
    auto take = [&](std::size_t n) -> const std::uint8_t* {
        if (n > in.size() - pos) {
            return nullptr;
        }
        const std::uint8_t* p = in.data() + pos;
        pos += n;
        return p;
    };
    auto u32 = [](const std::uint8_t* p) {
        return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
               (std::uint32_t{p[3]} << 24);
    };
    const std::uint8_t* magic = take(sizeof kTailsMagic);
    const std::uint8_t* count = magic ? take(4) : nullptr;
    if (!count || std::memcmp(magic, kTailsMagic, sizeof kTailsMagic) != 0) {
        return false;
    }
    for (std::uint32_t i = 0, n = u32(count); i < n; ++i) {
        const std::uint8_t* len = take(2);
        const std::uint8_t* name = len ? take(std::size_t{len[0]} | (std::size_t{len[1]} << 8)) : nullptr;
        const std::uint8_t* size = name ? take(4) : nullptr;
        const std::uint8_t* bytes = size ? take(u32(size)) : nullptr;
        if (!bytes && !(size && u32(size) == 0)) {
            return false;
        }
        out[std::string(reinterpret_cast<const char*>(name), std::size_t{len[0]} | (std::size_t{len[1]} << 8))] =
            std::vector<std::uint8_t>(bytes, bytes + u32(size));
    }
    return pos == in.size();
}

// ---- index.tsv rows with the columns the store needs ------------------------------------------------------------

struct IndexRow {
    std::string path;
    std::uint64_t size = 0;
    std::uint32_t crc32 = 0;
    std::string source;
    std::string format;
    std::vector<std::string> converted;
};

Result<std::vector<IndexRow>> read_index(const fs::path& gamedata_dir) {
    using R = Result<std::vector<IndexRow>>;
    const auto data = read_whole(gamedata_dir / "index.tsv");
    if (!data) {
        return R::fail("no index.tsv in " + gamedata_dir.string());
    }
    const std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
    std::vector<std::string_view> lines = split(text, '\n');
    if (lines.empty() || !lines.front().starts_with("path\tsize\tcrc32\tsource")) {
        return R::fail("index.tsv has no header");
    }
    std::vector<IndexRow> rows;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::string_view line = lines[i];
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string_view> cols = split(line, '\t');
        IndexRow row;
        if (cols.size() < 4 || !parse_u64(cols[1], row.size) || !parse_hex32(cols[2], row.crc32)) {
            return R::fail("index.tsv line " + std::to_string(i + 1) + " is malformed");
        }
        row.path = normalize(cols[0]);
        row.source = std::string(cols[3]);
        row.format = cols.size() > 4 ? std::string(cols[4]) : std::string();
        if (cols.size() > 5 && !cols[5].empty()) {
            for (const std::string_view out : split(cols[5], ';')) {
                if (!out.empty()) {
                    row.converted.emplace_back(out);
                }
            }
        }
        rows.push_back(std::move(row));
    }
    return R::ok(std::move(rows));
}

// The outputs of `row` that belong to the converter `kind`.
std::vector<std::string> outputs_of(const IndexRow& row, const ConverterKind& kind) {
    std::vector<std::string> out;
    for (const std::string& o : row.converted) {
        for (const std::string_view prefix : kind.prefixes) {
            if (o.starts_with(prefix)) {
                out.push_back(o);
                break;
            }
        }
    }
    return out;
}

Result<std::vector<std::uint8_t>> encode_from(const Converter& converter, const fs::path& convert_dir,
                                              const std::vector<std::string>& outputs) {
    std::vector<Output> files;
    files.reserve(outputs.size());
    for (const std::string& rel : outputs) {
        auto bytes = read_whole(convert_dir / fs::path(rel));
        if (!bytes) {
            return Result<std::vector<std::uint8_t>>::fail("cannot read convert/" + rel);
        }
        files.push_back({rel, std::move(*bytes)});
    }
    return converter.encode(files);
}

}  // namespace

// ---- Store -------------------------------------------------------------------------------------------------------

struct Store::Impl {
    fs::path gamedata_dir;
    std::vector<Entry> entries;
    std::unordered_map<std::string, std::size_t> by_path;
    std::unordered_map<std::string, std::vector<std::uint8_t>> tails;
    std::map<std::string, std::unique_ptr<Converter>, std::less<>> converters;

    struct Area {
        std::shared_ptr<const ByteSource> glb;
        std::unordered_map<std::string, level::UnifiedEntry> files;
    };
    mutable std::mutex area_mutex;
    mutable std::map<std::string, std::shared_ptr<const Area>> areas;  // by group; null when the glb is unusable

    std::shared_ptr<const Area> area(const std::string& group) const {
        std::lock_guard<std::mutex> lock(area_mutex);
        if (auto it = areas.find(group); it != areas.end()) {
            return it->second;
        }
        std::shared_ptr<Area> made;
        std::shared_ptr<const ByteSource> glb =
            open_file_source(gamedata_dir / fs::path(level::unified_relative_path(group)));
        if (glb) {
            if (auto index = level::read_unified_index(*glb)) {
                made = std::make_shared<Area>();
                made->glb = std::move(glb);
                for (const level::UnifiedEntry& e : index.value().files) {
                    made->files.emplace(normalize(e.game_path), e);
                }
            }
        }
        areas.emplace(group, made);
        return made;
    }
};

Store::Store(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Store::~Store() = default;

std::shared_ptr<const Store> Store::open(const fs::path& gamedata_dir, std::string& error) {
    const auto data = read_whole(gamedata_dir / kManifest);
    if (!data) {
        error = "no " + std::string(kManifest) + " in " + gamedata_dir.string();
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->gamedata_dir = gamedata_dir;
    const std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
    std::vector<std::string_view> lines = split(text, '\n');
    if (lines.empty() || lines.front() != kManifestHeader) {
        error = std::string(kManifest) + " has no header";
        return nullptr;
    }
    for (std::size_t i = 1; i < lines.size(); ++i) {
        if (lines[i].empty()) {
            continue;
        }
        const std::vector<std::string_view> cols = split(lines[i], '\t');
        Entry e;
        if (cols.size() != 6 || !parse_u64(cols[1], e.size) || !parse_u64(cols[2], e.stored) ||
            !parse_hex32(cols[3], e.crc32) || e.stored < e.size) {
            error = std::string(kManifest) + " line " + std::to_string(i + 1) + " is malformed";
            return nullptr;
        }
        e.path = normalize(cols[0]);
        if (cols[4] == "kept") {
            e.origin = Origin::Kept;
        } else if (cols[4] == "area") {
            e.origin = Origin::Area;
        } else {
            e.origin = Origin::Converter;
            e.converter = std::string(cols[4]);
            if (!impl->converters.contains(e.converter)) {
                std::unique_ptr<Converter> c = make_lossless(e.converter);
                if (!c) {
                    error =
                        std::string(kManifest) + " line " + std::to_string(i + 1) + ": unknown source " + e.converter;
                    return nullptr;
                }
                impl->converters.emplace(e.converter, std::move(c));
            }
            for (const std::string_view o : split(cols[5], ';')) {
                if (!o.empty()) {
                    e.outputs.emplace_back(o);
                }
            }
        }
        impl->by_path.emplace(e.path, impl->entries.size());
        impl->entries.push_back(std::move(e));
    }
    if (const auto tails = read_whole(gamedata_dir / kTails); !tails || !unpack_tails(*tails, impl->tails)) {
        error = std::string(kTails) + " is missing or broken";
        return nullptr;
    }
    return std::shared_ptr<const Store>(new Store(std::move(impl)));
}

const std::vector<Entry>& Store::entries() const { return impl_->entries; }

const Entry* Store::find(std::string_view path) const {
    const auto it = impl_->by_path.find(normalize(path));
    return it == impl_->by_path.end() ? nullptr : &impl_->entries[it->second];
}

Result<std::vector<std::uint8_t>> Store::read(const Entry& entry, bool* changed) const {
    using R = Result<std::vector<std::uint8_t>>;
    if (changed) {
        *changed = false;
    }
    switch (entry.origin) {
        case Origin::Kept: {
            if (entry.size == 0) {
                return R::ok({});
            }
            auto bytes = read_range(impl_->gamedata_dir / kStoreDir / fs::path(entry.path), 0, entry.size);
            if (!bytes) {
                return R::fail(std::string(kStoreDir) + "/" + entry.path + " is missing or shorter than " +
                               std::to_string(entry.size) + " bytes");
            }
            return R::ok(std::move(*bytes));
        }
        case Origin::Area: {
            const std::string group = level::unified_group_of(entry.path);
            const std::shared_ptr<const Impl::Area> area = impl_->area(group);
            const auto it = area ? area->files.find(entry.path) : decltype(area->files.end()){};
            if (!area || it == area->files.end()) {
                return R::fail(level::unified_relative_path(group) + " does not hold " + entry.path);
            }
            auto stored = level::read_unified_entry(*area->glb, it->second);
            if (!stored) {
                return R::fail(level::unified_relative_path(group) + ": " + stored.error());
            }
            std::vector<std::uint8_t> bytes = std::move(stored.value());
            bytes.resize(static_cast<std::size_t>(it->second.size));
            if (changed) {
                *changed = bytes.size() != entry.size || stored_crc(bytes, tail(entry)) != entry.crc32;
            }
            return R::ok(std::move(bytes));
        }
        case Origin::Converter: {
            const auto c = impl_->converters.find(entry.converter);
            if (c == impl_->converters.end()) {
                return R::fail("no converter " + entry.converter);
            }
            auto bytes = encode_from(*c->second, impl_->gamedata_dir / "convert", entry.outputs);
            if (!bytes) {
                return R::fail(entry.path + " (" + entry.converter + "): " + bytes.error());
            }
            if (changed) {
                *changed = bytes.value().size() != entry.size || stored_crc(bytes.value(), tail(entry)) != entry.crc32;
            }
            return bytes;
        }
    }
    return R::fail("unknown origin");
}

std::vector<std::uint8_t> Store::tail(const Entry& entry) const {
    const auto it = impl_->tails.find(entry.path);
    if (it != impl_->tails.end()) {
        return it->second;
    }
    return std::vector<std::uint8_t>(static_cast<std::size_t>(entry.stored - entry.size), 0);
}

std::unique_ptr<ByteSource> Store::open_kept(const Entry& entry) const {
    if (entry.origin != Origin::Kept) {
        return nullptr;
    }
    std::unique_ptr<ByteSource> file = open_file_source(impl_->gamedata_dir / kStoreDir / fs::path(entry.path));
    if (!file || file->size() < entry.size) {
        return nullptr;
    }
    return std::make_unique<WindowSource>(std::move(file), entry.size);
}

namespace {

std::mutex g_stores_mutex;
std::map<std::string, std::shared_ptr<const Store>>& stores() {
    static std::map<std::string, std::shared_ptr<const Store>> s;
    return s;
}

std::string store_key(const fs::path& gamedata_dir) {
    std::error_code ec;
    const fs::path canonical = fs::weakly_canonical(gamedata_dir, ec);
    return (ec ? gamedata_dir : canonical).generic_string();
}

}  // namespace

std::shared_ptr<const Store> store_for(const fs::path& gamedata_dir) {
    const std::string key = store_key(gamedata_dir);
    std::lock_guard<std::mutex> lock(g_stores_mutex);
    if (auto it = stores().find(key); it != stores().end()) {
        return it->second;
    }
    std::string error;
    std::shared_ptr<const Store> store = Store::open(gamedata_dir, error);
    stores().emplace(key, store);
    return store;
}

void forget(const fs::path& gamedata_dir) {
    std::lock_guard<std::mutex> lock(g_stores_mutex);
    stores().erase(store_key(gamedata_dir));
}

std::optional<std::vector<std::uint8_t>> read_game_file(const fs::path& gamedata_dir, std::string_view path) {
    if (const std::shared_ptr<const Store> store = store_for(gamedata_dir)) {
        if (const Entry* e = store->find(path)) {
            if (auto bytes = store->read(*e)) {
                std::vector<std::uint8_t> out = std::move(bytes.value());
                const std::vector<std::uint8_t> tail = store->tail(*e);
                out.insert(out.end(), tail.begin(), tail.end());
                return out;
            }
        }
    }
    return read_whole(gamedata_dir / "raw" / fs::path(normalize(path)));
}

bool has_raw(const fs::path& gamedata_dir) {
    std::error_code ec;
    return fs::is_directory(gamedata_dir / "raw", ec);
}

// ---- building the store from raw/ --------------------------------------------------------------------------------

Result<BuildReport> build_store(const fs::path& gamedata_dir, bool move, unsigned threads) {
    using R = Result<BuildReport>;
    const fs::path raw = gamedata_dir / "raw";
    if (!has_raw(gamedata_dir)) {
        return R::fail("no raw/ in " + gamedata_dir.string());
    }
    // Without raw/ the file table has no other copy: write it first.
    if (const Result<bool> table = filetable::write_from_raw(gamedata_dir, false); !table) {
        return R::fail("file table: " + table.error());
    }
    auto index = read_index(gamedata_dir);
    if (!index) {
        return R::fail(index.error());
    }
    const std::vector<IndexRow>& rows = index.value();

    // One converter per kind and thread is not needed: encode() is const and the converters keep no state.
    std::map<std::string, std::unique_ptr<Converter>, std::less<>> converters;
    for (const ConverterKind& kind : converter_kinds()) {
        converters.emplace(kind.name, make_lossless(kind.name));
    }
    const fs::path convert_dir = gamedata_dir / "convert";
    const fs::path store_dir = gamedata_dir / kStoreDir;

    struct Decision {
        enum class Kind { Skip, Kept, Converter, Area } kind = Kind::Kept;
        std::string converter;
        std::vector<std::string> outputs;
        std::vector<std::uint8_t> tail;  // the whole tail; sector_tails.bin keeps it when it is not all zeros
        std::uint64_t stored = 0;
        std::string note;
        std::string error;  // fatal
    };
    std::vector<Decision> decisions(rows.size());

    // Areas are opened once: many rows share a glb.
    std::mutex area_mutex;
    std::map<std::string, std::shared_ptr<const ByteSource>> glbs;
    std::map<std::string, std::optional<level::UnifiedIndex>> area_index;
    auto area_entry = [&](const std::string& group, const std::string& path,
                          std::shared_ptr<const ByteSource>& glb) -> std::optional<level::UnifiedEntry> {
        std::lock_guard<std::mutex> lock(area_mutex);
        auto it = area_index.find(group);
        if (it == area_index.end()) {
            std::optional<level::UnifiedIndex> idx;
            std::shared_ptr<const ByteSource> source =
                open_file_source(gamedata_dir / fs::path(level::unified_relative_path(group)));
            if (source) {
                if (auto read = level::read_unified_index(*source)) {
                    idx = std::move(read.value());
                    glbs[group] = source;
                }
            }
            it = area_index.emplace(group, std::move(idx)).first;
        }
        if (!it->second) {
            return std::nullopt;
        }
        glb = glbs[group];
        for (const level::UnifiedEntry& e : it->second->files) {
            if (normalize(e.game_path) == path) {
                return e;
            }
        }
        return std::nullopt;
    };

    auto decide = [&](std::size_t i) {
        const IndexRow& row = rows[i];
        Decision& d = decisions[i];
        // The executable and SYSTEM.CNF are not in the game's file table; tables/filetable.json holds the table.
        if (row.source.starts_with("ISO:") || row.source.starts_with("SLUS:")) {
            d.kind = Decision::Kind::Skip;
            return;
        }
        // XA voice and music tracks: the sound engine plays their WAVs; only the movies are read by sector.
        if (row.format == "xa" && !row.converted.empty()) {
            d.kind = Decision::Kind::Skip;
            return;
        }
        const fs::path file = raw / fs::path(row.path);
        std::error_code ec;
        const std::uintmax_t stored = fs::file_size(file, ec);
        if (ec || stored < row.size) {
            d.error = "raw/" + row.path + " is missing or shorter than index.tsv says";
            return;
        }
        d.stored = stored;
        if (stored > row.size) {
            auto tail = read_range(file, row.size, stored - row.size);
            if (!tail) {
                d.error = "cannot read raw/" + row.path;
                return;
            }
            d.tail = std::move(*tail);
        }
        for (const ConverterKind& kind : converter_kinds()) {
            std::vector<std::string> outputs = outputs_of(row, kind);
            if (outputs.empty()) {
                continue;
            }
            const auto rebuilt = encode_from(*converters.at(kind.name), convert_dir, outputs);
            if (rebuilt && rebuilt.value().size() == row.size && stored_crc(rebuilt.value(), d.tail) == row.crc32) {
                d.kind = Decision::Kind::Converter;
                d.converter = kind.name;
                d.outputs = std::move(outputs);
                return;
            }
            d.note = row.path + ": the " + std::string(kind.name) + " converter's files do not rebuild it (" +
                     (rebuilt ? std::string("different bytes") : rebuilt.error()) + "); kept as it is";
        }
        if (const std::string group = level::unified_group_of(row.path); !group.empty()) {
            std::shared_ptr<const ByteSource> glb;
            if (const auto e = area_entry(group, row.path, glb)) {
                auto bytes = level::read_unified_entry(*glb, *e);
                if (bytes && e->size == row.size && bytes.value().size() == d.stored &&
                    sh::crc32(bytes.value().data(), bytes.value().size()) == row.crc32) {
                    d.kind = Decision::Kind::Area;
                    return;
                }
            }
        }
        d.kind = Decision::Kind::Kept;
    };

    const unsigned workers =
        std::max(1u, std::min<unsigned>(threads ? threads : std::thread::hardware_concurrency(),
                                        static_cast<unsigned>(std::max<std::size_t>(rows.size(), 1))));
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < workers; ++t) {
        pool.emplace_back([&] {
            for (std::size_t i; (i = next.fetch_add(1)) < rows.size();) {
                decide(i);
            }
        });
    }
    for (std::thread& t : pool) {
        t.join();
    }

    BuildReport report;
    std::map<std::string, std::vector<std::uint8_t>> tails;
    std::string manifest = std::string(kManifestHeader) + "\n";
    std::error_code ec;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const IndexRow& row = rows[i];
        Decision& d = decisions[i];
        if (!d.error.empty()) {
            return R::fail(d.error);
        }
        if (!d.note.empty() && d.kind == Decision::Kind::Kept) {
            report.notes.push_back(d.note);
        }
        std::string source;
        std::string outputs;
        switch (d.kind) {
            case Decision::Kind::Skip:
                ++report.skipped;
                report.skipped_bytes += row.size;
                continue;
            case Decision::Kind::Converter:
                ++report.converted;
                source = d.converter;
                for (std::size_t k = 0; k < d.outputs.size(); ++k) {
                    outputs += (k ? ";" : "") + d.outputs[k];
                }
                break;
            case Decision::Kind::Area:
                ++report.areas;
                source = "area";
                break;
            case Decision::Kind::Kept: {
                ++report.kept;
                report.kept_bytes += row.size;
                source = "kept";
                const fs::path from = raw / fs::path(row.path);
                const fs::path to = store_dir / fs::path(row.path);
                fs::create_directories(to.parent_path(), ec);
                fs::remove(to, ec);
                ec.clear();
                if (move) {
                    fs::rename(from, to, ec);
                }
                if (!move || ec) {
                    ec.clear();
                    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
                }
                if (!ec) {
                    // The tail lives in sector_tails.bin; the kept file is the logical file.
                    fs::resize_file(to, row.size, ec);
                }
                if (ec) {
                    return R::fail("cannot write " + to.string() + ": " + ec.message());
                }
                break;
            }
        }
        if (std::any_of(d.tail.begin(), d.tail.end(), [](std::uint8_t b) { return b != 0; })) {
            ++report.tails;
            tails.emplace(row.path, std::move(d.tail));
        }
        manifest += row.path + "\t" + std::to_string(row.size) + "\t" + std::to_string(d.stored) + "\t" +
                    hex8(row.crc32) + "\t" + source + "\t" + outputs + "\n";
    }
    const std::vector<std::uint8_t> packed = pack_tails(tails);
    if (const Result<void> w = write_atomic(gamedata_dir / kTails, packed.data(), packed.size()); !w) {
        return R::fail(w.error());
    }
    // The manifest last: its presence marks a complete store.
    if (const Result<void> w = write_atomic(gamedata_dir / kManifest, manifest.data(), manifest.size()); !w) {
        return R::fail(w.error());
    }
    forget(gamedata_dir);
    return R::ok(std::move(report));
}

std::string format_build_line(const BuildReport& r) {
    auto mb = [](std::uint64_t bytes) { return std::to_string((bytes + (1u << 19)) >> 20) + " MB"; };
    std::string line = std::to_string(r.converted) + " rebuilt by converters, " + std::to_string(r.areas) +
                       " from maps/areas, " + std::to_string(r.kept) + " kept (" + mb(r.kept_bytes) + "), " +
                       std::to_string(r.skipped) + " not needed (" + mb(r.skipped_bytes) + "), " +
                       std::to_string(r.tails) + " sector tails";
    if (!r.notes.empty()) {
        line += ", " + std::to_string(r.notes.size()) + " kept because their converted files differ";
    }
    return line;
}

Result<std::uint64_t> remove_raw(const fs::path& gamedata_dir) {
    using R = Result<std::uint64_t>;
    if (!has_raw(gamedata_dir)) {
        return R::ok(0);
    }
    std::string error;
    if (!Store::open(gamedata_dir, error)) {
        return R::fail("raw/ is kept: " + error);
    }
    const std::uint64_t bytes = tree_bytes(gamedata_dir / "raw");
    std::error_code ec;
    fs::remove_all(gamedata_dir / "raw", ec);
    if (ec) {
        return R::fail("cannot delete " + (gamedata_dir / "raw").string() + ": " + ec.message());
    }
    return R::ok(bytes);
}

Result<UnreadReport> remove_unread_outputs(const fs::path& gamedata_dir) {
    using R = Result<UnreadReport>;
    const auto data = read_whole(gamedata_dir / "index.tsv");
    if (!data) {
        return R::fail("no index.tsv in " + gamedata_dir.string());
    }
    UnreadReport report;
    std::string text(data->begin(), data->end());
    std::string out;
    out.reserve(text.size());
    bool changed = false;
    std::error_code ec;
    for (const std::string_view line : split(text, '\n')) {
        std::vector<std::string_view> cols = split(line, '\t');
        if (cols.size() < 6 || cols[4] != "fmv" || cols[5].empty()) {
            out.append(line);
            out += '\n';
            continue;
        }
        std::string kept;
        for (const std::string_view o : split(cols[5], ';')) {
            if (o.ends_with(".wav")) {
                const fs::path file = gamedata_dir / "convert" / fs::path(std::string(o));
                const std::uintmax_t size = fs::file_size(file, ec);
                if (!ec && fs::remove(file, ec)) {
                    ++report.files;
                    report.bytes += size;
                }
                ec.clear();
                changed = true;
                continue;
            }
            kept += (kept.empty() ? "" : ";") + std::string(o);
        }
        for (std::size_t i = 0; i < cols.size(); ++i) {
            out += (i ? "\t" : "") + (i == 5 ? kept : std::string(cols[i]));
        }
        out += '\n';
    }
    // split() gives an empty last piece after the final newline; it must not become a second one.
    if (!text.empty() && text.back() == '\n' && out.size() >= 2 && out.ends_with("\n\n")) {
        out.pop_back();
    }
    if (changed) {
        if (const Result<void> w = write_atomic(gamedata_dir / "index.tsv", out.data(), out.size()); !w) {
            return R::fail(w.error());
        }
    }
    return R::ok(report);
}

Result<FinishReport> finish_import(const fs::path& gamedata_dir, bool keep_raw, unsigned threads) {
    using R = Result<FinishReport>;
    FinishReport report;
    if (!has_raw(gamedata_dir)) {
        return R::ok(report);
    }
    std::error_code ec;
    if (!fs::exists(gamedata_dir / kManifest, ec)) {
        auto built = build_store(gamedata_dir, !keep_raw, threads);
        if (!built) {
            return R::fail(built.error());
        }
        report.built = true;
        report.build = std::move(built.value());
    }
    if (!keep_raw) {
        auto removed = remove_raw(gamedata_dir);
        if (!removed) {
            return R::fail(removed.error());
        }
        report.removed = true;
        report.freed_bytes = removed.value();
        auto unread = remove_unread_outputs(gamedata_dir);
        if (!unread) {
            return R::fail(unread.error());
        }
        report.unread = unread.value();
    }
    return R::ok(std::move(report));
}

}  // namespace sh::convert::gamefiles
