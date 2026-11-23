#include "engine/maps/unified_layer.h"

#include "formats/common/byte_source.h"
#include "formats/convert/level/map_unified.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <system_error>
#include <vector>

namespace sh::maps {
namespace {

namespace fs = std::filesystem;
namespace lvl = sh::convert::level;

class BytesSource final : public ByteSource {
public:
    explicit BytesSource(std::vector<std::uint8_t> data) : data_(std::move(data)) {}
    std::uint64_t size() const override { return data_.size(); }
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

class UnifiedLayer final : public sh::vfs::Layer {
public:
    struct Entry {
        std::shared_ptr<const ByteSource> glb;
        lvl::UnifiedEntry entry;
        std::string file;  // for messages
    };

    UnifiedLayer(std::string name, std::map<std::string, Entry, std::less<>> entries, UnifiedLogFn log)
        : name_(std::move(name)), entries_(std::move(entries)), log_(std::move(log)) {}

    std::string name() const override { return name_; }

    bool exists(std::string_view path) const override { return entries_.count(sh::vfs::normalize(path)) != 0; }

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override {
        auto stored = read_stored(path);
        if (!stored) {
            return std::nullopt;
        }
        const Entry& e = entries_.find(sh::vfs::normalize(path))->second;
        stored->resize(static_cast<std::size_t>(e.entry.size));
        return stored;
    }

    std::unique_ptr<ByteSource> open_stored(std::string_view path) const override {
        auto stored = read_stored(path);
        return stored ? std::make_unique<BytesSource>(std::move(*stored)) : nullptr;
    }

    std::vector<std::string> list(std::string_view dir) const override {
        std::string prefix = sh::vfs::normalize(dir);
        if (!prefix.empty() && prefix.back() != '/') {
            prefix += '/';
        }
        std::vector<std::string> out;
        for (const auto& [path, e] : entries_) {
            if (path.compare(0, prefix.size(), prefix) == 0 && path.find('/', prefix.size()) == std::string::npos) {
                out.push_back(path.substr(prefix.size()));
            }
        }
        return out;
    }

private:
    std::optional<std::vector<std::uint8_t>> read_stored(std::string_view path) const {
        const auto it = entries_.find(sh::vfs::normalize(path));
        if (it == entries_.end()) {
            return std::nullopt;
        }
        auto bytes = lvl::read_unified_entry(*it->second.glb, it->second.entry);
        if (!bytes) {
            if (log_) {
                log_(1, "[MAPS] " + it->second.file + ": " + bytes.error());
            }
            return std::nullopt;
        }
        return std::move(bytes.value());
    }

    std::string name_;
    std::map<std::string, Entry, std::less<>> entries_;
    UnifiedLogFn log_;
};

}  // namespace

std::unique_ptr<sh::vfs::Layer> make_unified_maps_layer(const fs::path& root, std::string name,
                                                        const UnifiedLogFn& log) {
    const fs::path dir = root / "maps" / "areas";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return nullptr;
    }
    std::vector<fs::path> files;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == ".glb") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());

    std::map<std::string, UnifiedLayer::Entry, std::less<>> entries;
    std::size_t count = 0;
    for (const fs::path& file : files) {
        const std::string shown = "maps/areas/" + file.filename().generic_string();
        std::shared_ptr<const ByteSource> source = open_file_source(file);
        if (!source) {
            if (log) {
                log(1, "[MAPS] " + shown + ": cannot be opened");
            }
            continue;
        }
        auto index = lvl::read_unified_index(*source);
        if (!index) {
            if (log) {
                log(1, "[MAPS] " + shown + ": " + index.error() + "; its files come from the layers below");
            }
            continue;
        }
        ++count;
        for (lvl::UnifiedEntry& e : index.value().files) {
            std::string path = sh::vfs::normalize(e.game_path);
            // Two files packing the same path: the first by name wins, like a directory listing.
            entries.emplace(std::move(path), UnifiedLayer::Entry{source, std::move(e), shown});
        }
    }
    if (entries.empty()) {
        return nullptr;
    }
    if (log) {
        log(0, "[MAPS] " + name + ": " + std::to_string(count) + " unified map files, " + std::to_string(entries.size()) +
                   " map files (" + dir.generic_string() + ")");
    }
    return std::make_unique<UnifiedLayer>(std::move(name), std::move(entries), log);
}

}  // namespace sh::maps
