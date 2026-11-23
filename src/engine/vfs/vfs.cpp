#include "engine/vfs/vfs.h"

#include "engine/manifest/map_manifest.h"
#include "formats/archive/sh_archive.h"
#include "formats/common/byte_source.h"
#include "formats/iso9660/iso9660.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace sh::vfs {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kIsoBlockBytes = 2048;

// The disc identity (version.cfg game id) comes from the game manifest with
// this disc's default as fallback.
std::string game_id() {
    const char* id = MapManifest_DiscString("game_id");
    return id != nullptr ? id : "SLUS-007.07";
}


char to_upper_ascii(char c) {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - ('a' - 'A')) : c;
}

std::uint32_t iso_block_count(std::uint32_t bytes) {
    return static_cast<std::uint32_t>((std::uint64_t{bytes} + kIsoBlockBytes - 1) / kIsoBlockBytes);
}

std::optional<std::vector<std::uint8_t>> read_file(const fs::path& file) {
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(file, ec);
    if (ec) {
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty() &&
        !in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
        return std::nullopt;
    }
    return data;
}

std::optional<std::vector<std::uint8_t>> read_source(const ByteSource& source, std::uint64_t size) {
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty() && !source.read_at(0, data.data(), data.size())) {
        return std::nullopt;
    }
    return data;
}

class MemorySource final : public ByteSource {
public:
    explicit MemorySource(std::vector<std::uint8_t> data) : data_(std::move(data)) {}

    std::uint64_t size() const override {
        return data_.size();
    }

    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > data_.size() || count > data_.size() - offset) {
            return false;
        }
        if (count != 0) {
            std::memcpy(dst, data_.data() + offset, count);
        }
        return true;
    }

private:
    std::vector<std::uint8_t> data_;
};

// The first `size` bytes from `offset` of another source.
class WindowSource final : public ByteSource {
public:
    WindowSource(std::shared_ptr<const ByteSource> base, std::uint64_t offset, std::uint64_t size)
        : base_(std::move(base)), offset_(offset), size_(size) {}

    std::uint64_t size() const override {
        return size_;
    }

    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > size_ || count > size_ - offset) {
            return false;
        }
        return base_->read_at(offset_ + offset, dst, count);
    }

private:
    std::shared_ptr<const ByteSource> base_;
    std::uint64_t offset_;
    std::uint64_t size_;
};

// Children of `dir` among canonical file paths: "NAME" for files, "NAME/" for folders.
template <typename Keys>
std::vector<std::string> list_children(const Keys& keys, std::string_view dir) {
    const std::string prefix = normalize(dir).empty() ? std::string() : normalize(dir) + "/";
    std::set<std::string> out;
    for (const auto& entry : keys) {
        const std::string& key = entry.first;
        if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        const std::size_t slash = key.find('/', prefix.size());
        out.insert(slash == std::string::npos ? key.substr(prefix.size())
                                              : key.substr(prefix.size(), slash + 1 - prefix.size()));
    }
    return {out.begin(), out.end()};
}

class DirectoryLayer final : public Layer {
public:
    DirectoryLayer(std::string name, std::unordered_map<std::string, fs::path> files)
        : name_(std::move(name)), files_(std::move(files)) {}

    std::string name() const override {
        return name_;
    }

    bool exists(std::string_view path) const override {
        return files_.contains(normalize(path));
    }

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override {
        const auto it = files_.find(normalize(path));
        if (it == files_.end()) {
            return std::nullopt;
        }
        return read_file(it->second);
    }

    std::unique_ptr<ByteSource> open(std::string_view path) const override {
        const auto it = files_.find(normalize(path));
        return it == files_.end() ? nullptr : open_file_source(it->second);
    }

    std::vector<std::string> list(std::string_view dir) const override {
        return list_children(files_, dir);
    }

    fs::path host_path(std::string_view path) const override {
        const auto it = files_.find(normalize(path));
        return it == files_.end() ? fs::path() : it->second;
    }

private:
    std::string name_;
    std::unordered_map<std::string, fs::path> files_;
};

struct StoredFile {
    fs::path file;
    std::uint64_t size;  // logical
};

class GameDataLayer final : public Layer {
public:
    GameDataLayer(std::string name, std::unordered_map<std::string, StoredFile> files)
        : name_(std::move(name)), files_(std::move(files)) {}

    std::string name() const override {
        return name_;
    }

    bool exists(std::string_view path) const override {
        return files_.contains(normalize(path));
    }

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override {
        const std::unique_ptr<ByteSource> source = open(path);
        if (!source) {
            return std::nullopt;
        }
        return read_source(*source, source->size());
    }

    std::unique_ptr<ByteSource> open(std::string_view path) const override {
        const auto it = files_.find(normalize(path));
        if (it == files_.end()) {
            return nullptr;
        }
        std::shared_ptr<const ByteSource> stored = open_file_source(it->second.file);
        // A raw file shorter than its logical size is damaged, not a shorter version of the file.
        if (!stored || stored->size() < it->second.size) {
            return nullptr;
        }
        return std::make_unique<WindowSource>(std::move(stored), 0, it->second.size);
    }

    std::unique_ptr<ByteSource> open_stored(std::string_view path) const override {
        const auto it = files_.find(normalize(path));
        if (it == files_.end()) {
            return nullptr;
        }
        std::unique_ptr<ByteSource> stored = open_file_source(it->second.file);
        if (!stored || stored->size() < it->second.size) {
            return nullptr;
        }
        return stored;
    }

    std::vector<std::string> list(std::string_view dir) const override {
        return list_children(files_, dir);
    }

    fs::path host_path(std::string_view path) const override {
        const auto it = files_.find(normalize(path));
        if (it == files_.end()) {
            return {};
        }
        std::error_code ec;
        return fs::file_size(it->second.file, ec) == it->second.size && !ec ? it->second.file : fs::path();
    }

private:
    std::string name_;
    std::unordered_map<std::string, StoredFile> files_;
};

// A file found by the ISO 9660 walk, or an index into the archive table.
using DiscLocation = std::variant<iso9660::FileInfo, std::size_t>;

std::optional<std::vector<std::uint8_t>> read_iso_file(const iso9660::Volume& volume,
                                                       const iso9660::FileInfo& file) {
    const std::unique_ptr<ByteSource> source = volume.open_file(file);
    if (!source) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> data(source->size());
    if (!data.empty() && !source->read_at(0, data.data(), data.size())) {
        return std::nullopt;
    }
    return data;
}

class DiscLayer final : public Layer {
public:
    DiscLayer(std::string name, std::optional<iso9660::Volume> volume,
              std::optional<archive::Archive> silent_archive, std::optional<archive::Archive> hill_archive,
              std::vector<archive::Entry> entries, std::unordered_map<std::string, DiscLocation> index)
        : name_(std::move(name)),
          volume_(std::move(volume)),
          silent_archive_(std::move(silent_archive)),
          hill_archive_(std::move(hill_archive)),
          entries_(std::move(entries)),
          index_(std::move(index)) {}

    std::string name() const override {
        return name_;
    }

    bool exists(std::string_view path) const override {
        return index_.contains(normalize(path));
    }

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override {
        const auto it = index_.find(normalize(path));
        if (it == index_.end()) {
            return std::nullopt;
        }
        if (const auto* file = std::get_if<iso9660::FileInfo>(&it->second)) {
            return read_iso_file(*volume_, *file);
        }
        const archive::Entry& entry = entries_[std::get<std::size_t>(it->second)];
        const std::optional<archive::Archive>& container =
            entry.region == archive::Region::Silent ? silent_archive_ : hill_archive_;
        if (!container) {
            return std::nullopt;
        }
        return container->read(entry);
    }

    std::vector<std::string> list(std::string_view dir) const override {
        return list_children(index_, dir);
    }

private:
    std::string name_;
    std::optional<iso9660::Volume> volume_;
    std::optional<archive::Archive> silent_archive_;
    std::optional<archive::Archive> hill_archive_;
    std::vector<archive::Entry> entries_;
    std::unordered_map<std::string, DiscLocation> index_;
};

}  // namespace

std::string normalize(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) {
            end = path.size();
        }
        const std::string_view part = path.substr(start, end - start);
        if (!part.empty() && part != ".") {
            if (!out.empty()) {
                out.push_back('/');
            }
            for (const char c : part) {
                out.push_back(to_upper_ascii(c));
            }
        }
        start = end + 1;
    }
    return out;
}

void Vfs::add_layer(std::unique_ptr<Layer> layer, int priority) {
    if (!layer) {
        return;
    }
    const auto pos = std::upper_bound(slots_.begin(), slots_.end(), priority,
                                      [](int value, const Slot& slot) { return value > slot.priority; });
    slots_.insert(pos, Slot{priority, std::move(layer)});
}

bool Vfs::exists(std::string_view path) const {
    return std::any_of(slots_.begin(), slots_.end(),
                       [path](const Slot& slot) { return slot.layer->exists(path); });
}

std::optional<std::vector<std::uint8_t>> Vfs::read(std::string_view path) const {
    // The first layer that has the file answers even if its read fails: a broken mod file must
    // not silently fall back to the stock one.
    for (const Slot& slot : slots_) {
        if (slot.layer->exists(path)) {
            return slot.layer->read(path);
        }
    }
    return std::nullopt;
}

std::unique_ptr<ByteSource> Vfs::open(std::string_view path) const {
    const Layer* layer = find(path);
    return layer ? layer->open(path) : nullptr;
}

std::vector<std::string> Vfs::list(std::string_view dir) const {
    std::set<std::string> names;
    for (const Slot& slot : slots_) {
        for (std::string& name : slot.layer->list(dir)) {
            names.insert(std::move(name));
        }
    }
    return {names.begin(), names.end()};
}

const Layer* Vfs::find(std::string_view path) const {
    for (const Slot& slot : slots_) {
        if (slot.layer->exists(path)) {
            return slot.layer.get();
        }
    }
    return nullptr;
}

std::unique_ptr<ByteSource> Layer::open(std::string_view path) const {
    std::optional<std::vector<std::uint8_t>> data = read(path);
    return data ? std::make_unique<MemorySource>(std::move(*data)) : nullptr;
}

std::unique_ptr<ByteSource> Layer::open_stored(std::string_view path) const {
    return open(path);
}

std::vector<std::string> Layer::list(std::string_view) const {
    return {};
}

fs::path Layer::host_path(std::string_view) const {
    return {};
}

std::vector<std::string> Vfs::layer_names() const {
    std::vector<std::string> names;
    names.reserve(slots_.size());
    for (const Slot& slot : slots_) {
        names.push_back(slot.layer->name());
    }
    return names;
}

std::unique_ptr<Layer> make_directory_layer(fs::path root, std::string name) {
    std::vector<std::pair<std::string, fs::path>> found;
    std::error_code ec;
    const fs::path base = fs::canonical(root, ec);
    if (!ec) {
        // Each real folder is walked once. Symlinks to folders are followed, so a link back to an
        // ancestor must not loop: its target is already in `visited`.
        std::set<fs::path> visited{base};
        const auto options =
            fs::directory_options::follow_directory_symlink | fs::directory_options::skip_permission_denied;
        fs::recursive_directory_iterator it(base, options, ec);
        for (const fs::recursive_directory_iterator end{}; !ec && it != end; it.increment(ec)) {
            std::error_code type_ec;
            const fs::directory_entry& entry = *it;
            if (entry.is_directory(type_ec)) {
                std::error_code canonical_ec;
                const fs::path real = fs::canonical(entry.path(), canonical_ec);
                if (canonical_ec || !visited.insert(real).second) {
                    it.disable_recursion_pending();
                }
            } else if (entry.is_regular_file(type_ec)) {
                found.emplace_back(normalize(entry.path().lexically_relative(base).generic_string()), entry.path());
            }
        }
    }

    // Names that differ only by case collide. Sorting by real path makes the winner independent of
    // the order the directory was listed in.
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    std::unordered_map<std::string, fs::path> files;
    for (auto& [key, real] : found) {
        files.emplace(key, std::move(real));
    }
    return std::make_unique<DirectoryLayer>(std::move(name), std::move(files));
}

std::unique_ptr<Layer> make_disc_layer(std::shared_ptr<const disc::Image> image) {
    std::optional<iso9660::Volume> volume;
    if (image) {
        volume = iso9660::Volume::open(image);
    }
    if (!volume) {
        return std::make_unique<DiscLayer>("disc: no ISO 9660 volume", std::optional<iso9660::Volume>{},
                                           std::optional<archive::Archive>{}, std::optional<archive::Archive>{},
                                           std::vector<archive::Entry>{},
                                           std::unordered_map<std::string, DiscLocation>{});
    }

    std::unordered_map<std::string, DiscLocation> index;

    // A damaged image can point a folder back at an ancestor, so each folder is listed once.
    std::set<std::uint32_t> listed;
    std::vector<std::string> pending{""};
    while (!pending.empty()) {
        const std::string dir = std::move(pending.back());
        pending.pop_back();
        for (const iso9660::FileInfo& file : volume->list(dir)) {
            const std::string path = dir.empty() ? file.name : dir + "/" + file.name;
            if (!file.directory) {
                index.emplace(normalize(path), file);
            } else if (listed.insert(file.lba).second) {
                pending.push_back(path);
            }
        }
    }

    // The game's file table lives in the disc executable and points into both containers, so the table is
    // read from the executable and each container is laid out by its ISO 9660 extent. The names come
    // from the game manifest (gamedata/game.json disc{}) with this disc's defaults as fallback.
    std::string name = "disc";
    std::optional<archive::Archive> silent_archive;
    std::optional<archive::Archive> hill_archive;
    std::vector<archive::Entry> entries;
    const char* manifest_container = MapManifest_DiscContainer(0);
    const char* manifest_hill = MapManifest_DiscContainer(1);
    const char* manifest_exe = MapManifest_DiscString("exe_file");
    const std::string container_name = manifest_container != nullptr ? manifest_container : "SILENT.";
    const std::string hill_name = manifest_hill != nullptr ? manifest_hill : "HILL.";
    const std::string exe_name = manifest_exe != nullptr ? manifest_exe : "SLUS_007.07";
    const auto silent_file = volume->find(container_name);
    const auto hill_file = volume->find(hill_name);
    const auto exe_file = volume->find(exe_name);
    if (silent_file && hill_file && exe_file && !silent_file->directory && !hill_file->directory &&
        !exe_file->directory) {
        const std::unique_ptr<ByteSource> exe = volume->open_file(*exe_file);
        const archive::Layout silent_layout{silent_file->lba, iso_block_count(silent_file->size)};
        const archive::Layout hill_layout{hill_file->lba, iso_block_count(hill_file->size)};
        std::optional<std::vector<archive::Entry>> table;
        if (exe) {
            table = archive::read_file_table(*exe, silent_layout, hill_layout);
        }
        if (table) {
            entries = std::move(*table);
            silent_archive = archive::Archive::open(
                std::shared_ptr<const ByteSource>(volume->open_file(*silent_file)), archive::Region::Silent);
            hill_archive = archive::Archive::open(
                std::shared_ptr<const ByteSource>(archive::open_hill(image, hill_layout)), archive::Region::Hill);
            for (std::size_t i = 0; i < entries.size(); ++i) {
                index.emplace(normalize(entries[i].name), i);
            }
        } else {
            name = "disc: no file table (not " + game_id() + "?)";
        }
    } else {
        name = "disc: no " + container_name + "/" + hill_name;
    }

    // Names that collide keep their first entry: ISO files before the archive table, and table
    // entries in file-table order.
    return std::make_unique<DiscLayer>(std::move(name), std::move(volume), std::move(silent_archive),
                                       std::move(hill_archive), std::move(entries), std::move(index));
}

namespace {

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

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

// Value of `key` in a "key = value" file, or empty.
std::string cfg_value(const fs::path& file, std::string_view key) {
    const std::optional<std::vector<std::uint8_t>> data = read_file(file);
    if (!data) {
        return {};
    }
    const std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
    for (const std::string_view line : split(text, '\n')) {
        const std::size_t eq = line.find('=');
        if (eq != std::string_view::npos && trim(line.substr(0, eq)) == key) {
            return std::string(trim(line.substr(eq + 1)));
        }
    }
    return {};
}

}  // namespace

std::vector<GameDataFile> read_gamedata_index(const fs::path& gamedata_dir, std::string& error) {
    const std::optional<std::vector<std::uint8_t>> data = read_file(gamedata_dir / "index.tsv");
    if (!data) {
        error = "no index.tsv in " + gamedata_dir.string();
        return {};
    }
    const std::string_view text(reinterpret_cast<const char*>(data->data()), data->size());
    std::vector<std::string_view> lines = split(text, '\n');
    if (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    if (lines.empty() || !lines.front().starts_with("path\tsize\tcrc32\tsource")) {
        error = "index.tsv has no header";
        return {};
    }

    std::vector<GameDataFile> rows;
    rows.reserve(lines.size() - 1);
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::vector<std::string_view> cols = split(lines[i], '\t');
        GameDataFile row;
        std::uint64_t size = 0;
        bool digits = cols.size() >= 4 && !cols[1].empty();
        for (std::size_t c = 0; digits && c < cols[1].size(); ++c) {
            digits = cols[1][c] >= '0' && cols[1][c] <= '9';
            size = size * 10 + static_cast<std::uint64_t>(cols[1][c] - '0');
        }
        if (!digits || cols[0].empty()) {
            error = "index.tsv line " + std::to_string(i + 1) + " is malformed";
            return {};
        }
        row.path = normalize(cols[0]);
        row.size = size;
        row.source = std::string(cols[3]);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::unique_ptr<Layer> make_gamedata_layer(const fs::path& gamedata_dir, std::string& error) {
    // version.cfg is written last by the import, so its presence is what marks a finished import.
    const std::string game = cfg_value(gamedata_dir / "version.cfg", "game");
    if (game.empty()) {
        error = "no finished import in " + gamedata_dir.string() + " (version.cfg missing)";
        return nullptr;
    }
    if (game != game_id()) {
        error = "gamedata holds " + game + ", only " + game_id() + " is supported";
        return nullptr;
    }
    std::vector<GameDataFile> rows = read_gamedata_index(gamedata_dir, error);
    if (rows.empty()) {
        if (error.empty()) {
            error = "index.tsv is empty";
        }
        return nullptr;
    }
    std::unordered_map<std::string, StoredFile> files;
    files.reserve(rows.size());
    const fs::path raw = gamedata_dir / "raw";
    for (const GameDataFile& row : rows) {
        files.emplace(row.path, StoredFile{raw / fs::path(row.path), row.size});
    }
    return std::make_unique<GameDataLayer>("gamedata: " + gamedata_dir.string(), std::move(files));
}

}  // namespace sh::vfs
