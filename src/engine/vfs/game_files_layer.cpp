#include "engine/vfs/game_files_layer.h"

#include "formats/convert/gamefiles/game_files.h"

#include <cstring>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace sh::vfs {

namespace {

namespace fs = std::filesystem;
namespace gf = sh::convert::gamefiles;

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

class GameFilesLayer final : public Layer {
public:
    GameFilesLayer(std::string name, std::shared_ptr<const gf::Store> store, GameFilesLogFn log)
        : name_(std::move(name)), store_(std::move(store)), log_(std::move(log)) {}

    std::string name() const override { return name_; }

    bool exists(std::string_view path) const override { return store_->find(path) != nullptr; }

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override {
        const gf::Entry* e = store_->find(path);
        if (!e) {
            return std::nullopt;
        }
        bool changed = false;
        auto bytes = store_->read(*e, &changed);
        if (!bytes) {
            warn_once(e->path, e->path + ": " + bytes.error());
            return std::nullopt;
        }
        if (changed) {
            warn_once(e->path, e->path + ": rebuilt from edited converted files (differs from the disc)");
        }
        return std::move(bytes.value());
    }

    std::unique_ptr<ByteSource> open(std::string_view path) const override {
        const gf::Entry* e = store_->find(path);
        if (!e) {
            return nullptr;
        }
        // Movies are big: a kept file is streamed, not read whole.
        if (std::unique_ptr<ByteSource> kept = store_->open_kept(*e)) {
            return kept;
        }
        auto bytes = read(path);
        return bytes ? std::make_unique<BytesSource>(std::move(*bytes)) : nullptr;
    }

    std::unique_ptr<ByteSource> open_stored(std::string_view path) const override {
        const gf::Entry* e = store_->find(path);
        if (!e) {
            return nullptr;
        }
        std::vector<std::uint8_t> tail = store_->tail(*e);
        if (tail.empty()) {
            return open(path);
        }
        auto bytes = read(path);
        if (!bytes) {
            return nullptr;
        }
        bytes->insert(bytes->end(), tail.begin(), tail.end());
        return std::make_unique<BytesSource>(std::move(*bytes));
    }

    std::vector<std::string> list(std::string_view dir) const override {
        const std::string n = normalize(dir);
        const std::string prefix = n.empty() ? std::string() : n + "/";
        std::set<std::string> out;
        for (const gf::Entry& e : store_->entries()) {
            if (e.path.size() <= prefix.size() || e.path.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            const std::size_t slash = e.path.find('/', prefix.size());
            out.insert(slash == std::string::npos ? e.path.substr(prefix.size())
                                                  : e.path.substr(prefix.size(), slash + 1 - prefix.size()));
        }
        return {out.begin(), out.end()};
    }

private:
    void warn_once(const std::string& key, const std::string& message) const {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!warned_.insert(key).second) {
                return;
            }
        }
        if (log_) {
            log_(1, message);
        }
    }

    std::string name_;
    std::shared_ptr<const gf::Store> store_;
    GameFilesLogFn log_;
    mutable std::mutex mutex_;
    mutable std::set<std::string> warned_;
};

}  // namespace

std::unique_ptr<Layer> make_game_files_layer(const fs::path& gamedata_dir, GameFilesLogFn log, std::string& error) {
    gf::forget(gamedata_dir);
    std::shared_ptr<const gf::Store> store = gf::Store::open(gamedata_dir, error);
    if (!store) {
        return nullptr;
    }
    return std::make_unique<GameFilesLayer>("game files: " + (gamedata_dir / gf::kStoreDir).string(), std::move(store),
                                            std::move(log));
}

}  // namespace sh::vfs
