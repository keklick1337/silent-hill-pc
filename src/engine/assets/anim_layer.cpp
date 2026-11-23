#include "engine/assets/anim_layer.h"

#include "formats/convert/anim/anim_converter.h"
#include "formats/convert/anim/anim_json.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <system_error>
#include <vector>

namespace sh::assets {
namespace {

namespace fs = std::filesystem;
namespace an = sh::convert::anim;

constexpr std::string_view kSuffix = ".anim.json";

class BytesSource final : public ByteSource {
public:
    explicit BytesSource(std::shared_ptr<const std::vector<std::uint8_t>> data) : data_(std::move(data)) {}
    std::uint64_t size() const override { return data_->size(); }
    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > data_->size() || count > data_->size() - offset) {
            return false;
        }
        if (count != 0) {
            std::memcpy(dst, data_->data() + offset, count);
        }
        return true;
    }

private:
    std::shared_ptr<const std::vector<std::uint8_t>> data_;
};

std::optional<std::string> read_text(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return std::nullopt;
    }
    return text;
}

class AnimJsonLayer final : public sh::vfs::Layer {
public:
    struct Entry {
        fs::path file;
        std::string shown;  // for messages
        // Filled on first read: the file bytes, then the same bytes with the sector tail. Null after a failure.
        mutable bool loaded = false;
        mutable std::shared_ptr<const std::vector<std::uint8_t>> logical;
        mutable std::shared_ptr<const std::vector<std::uint8_t>> stored;
    };

    AnimJsonLayer(std::string name, std::map<std::string, Entry, std::less<>> entries, const sh::vfs::Layer* tail_from,
                  AssetLogFn log)
        : name_(std::move(name)), entries_(std::move(entries)), tail_from_(tail_from), log_(std::move(log)) {}

    std::string name() const override { return name_; }

    bool exists(std::string_view path) const override {
        const Entry* e = load(path);
        return e && e->logical;
    }

    std::optional<std::vector<std::uint8_t>> read(std::string_view path) const override {
        const Entry* e = load(path);
        if (!e || !e->logical) {
            return std::nullopt;
        }
        return *e->logical;
    }

    std::unique_ptr<ByteSource> open(std::string_view path) const override {
        const Entry* e = load(path);
        return e && e->logical ? std::make_unique<BytesSource>(e->logical) : nullptr;
    }

    std::unique_ptr<ByteSource> open_stored(std::string_view path) const override {
        const Entry* e = load(path);
        return e && e->stored ? std::make_unique<BytesSource>(e->stored) : nullptr;
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
    const Entry* load(std::string_view path) const {
        const std::string key = sh::vfs::normalize(path);
        const auto it = entries_.find(key);
        if (it == entries_.end()) {
            return nullptr;
        }
        const Entry& e = it->second;
        std::lock_guard<std::mutex> lock(mutex_);
        if (e.loaded) {
            return &e;
        }
        e.loaded = true;
        const std::optional<std::string> text = read_text(e.file);
        if (!text) {
            warn(e.shown + ": cannot be read; " + key + " comes from the layers below");
            return &e;
        }
        Result<an::AnmFile> file = an::anm_from_json(*text);
        if (!file) {
            warn(e.shown + ": " + file.error() + "; " + key + " comes from the layers below");
            return &e;
        }
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(file.value().serialize());
        e.logical = bytes;
        e.stored = bytes;
        if (tail_from_) {
            // The sector tail of the disc file, only behind a file of the disc file's size.
            const std::optional<std::vector<std::uint8_t>> disc = tail_from_->read(key);
            std::unique_ptr<ByteSource> sectors = disc ? tail_from_->open_stored(key) : nullptr;
            if (sectors && disc->size() == bytes->size() && sectors->size() > bytes->size()) {
                auto with_tail = std::make_shared<std::vector<std::uint8_t>>(*bytes);
                with_tail->resize(static_cast<std::size_t>(sectors->size()));
                if (sectors->read_at(bytes->size(), with_tail->data() + bytes->size(),
                                     with_tail->size() - bytes->size())) {
                    e.stored = std::move(with_tail);
                }
            }
        }
        return &e;
    }

    void warn(const std::string& message) const {
        if (log_) {
            log_(1, "[ANIM] " + message);
        }
    }

    std::string name_;
    std::map<std::string, Entry, std::less<>> entries_;
    const sh::vfs::Layer* tail_from_;
    AssetLogFn log_;
    mutable std::mutex mutex_;
};

}  // namespace

std::string anim_game_path(const fs::path& relative) {
    std::string s = relative.generic_string();
    if (s.size() <= kSuffix.size()) {
        return {};
    }
    std::string tail = s.substr(s.size() - kSuffix.size());
    std::transform(tail.begin(), tail.end(), tail.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (tail != kSuffix) {
        return {};
    }
    return sh::vfs::normalize(s.substr(0, s.size() - kSuffix.size()) + ".ANM");
}

std::unique_ptr<sh::vfs::Layer> make_anim_json_layer(const fs::path& root, std::string name,
                                                     const sh::vfs::Layer* tail_from, const AssetLogFn& log) {
    const fs::path dir = root / "convert" / "anim";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return nullptr;
    }
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && !anim_game_path(fs::relative(it->path(), dir, ec)).empty()) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());

    std::map<std::string, AnimJsonLayer::Entry, std::less<>> entries;
    for (const fs::path& file : files) {
        const fs::path rel = fs::relative(file, dir, ec);
        AnimJsonLayer::Entry e;
        e.file = file;
        e.shown = "convert/anim/" + rel.generic_string();
        // Two spellings of one path (case): the first by name wins, like a directory listing.
        entries.emplace(anim_game_path(rel), std::move(e));
    }
    if (entries.empty()) {
        return nullptr;
    }
    if (log) {
        log(0, "[ANIM] " + name + ": " + std::to_string(entries.size()) + " animations (" + dir.generic_string() + ")");
    }
    return std::make_unique<AnimJsonLayer>(std::move(name), std::move(entries), tail_from, log);
}

int write_anim_json_from_raw(const fs::path& gamedata_dir, const sh::vfs::Layer& raw, const AssetLogFn& log) {
    std::string error;
    const std::vector<sh::vfs::GameDataFile> index = sh::vfs::read_gamedata_index(gamedata_dir, error);
    if (index.empty()) {
        if (log) {
            log(1, "[ANIM] index.tsv: " + error);
        }
        return 0;
    }
    const auto converter = an::make_converter([&](std::string_view p) { return raw.read(p); });
    int written = 0;
    for (const sh::vfs::GameDataFile& f : index) {
        const std::string& p = f.path;
        if (p.size() < 4 || p.compare(p.size() - 4, 4, ".ANM") != 0) {
            continue;
        }
        const std::optional<std::vector<std::uint8_t>> bytes = raw.read(p);
        if (!bytes || !converter->accepts(*bytes, p)) {
            continue;  // the empty ANM files of the disc
        }
        auto out = converter->decode(*bytes, p);
        if (!out) {
            if (log) {
                log(1, "[ANIM] " + out.error());
            }
            continue;
        }
        for (const sh::convert::Output& o : out.value()) {
            const fs::path target = gamedata_dir / "convert" / o.relative_path;
            const fs::path temp = target.string() + ".tmp";
            std::error_code ec;
            fs::create_directories(target.parent_path(), ec);
            {
                std::ofstream os(temp, std::ios::binary | std::ios::trunc);
                os.write(reinterpret_cast<const char*>(o.bytes.data()), static_cast<std::streamsize>(o.bytes.size()));
                if (!os) {
                    fs::remove(temp, ec);
                    continue;
                }
            }
            fs::rename(temp, target, ec);
            if (!ec) {
                ++written;
            }
        }
    }
    return written;
}

}  // namespace sh::assets
