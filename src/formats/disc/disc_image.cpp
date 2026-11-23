#include "formats/disc/disc_image.h"

#include "formats/common/byte_source.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sh::disc {

struct Image::Impl {
    std::unique_ptr<ByteSource> source;
    std::uint32_t sectors = 0;
    bool sync_ok = false;
};

namespace {

constexpr std::uint64_t kRawSectorSize = 2352;
constexpr std::uint64_t kUserDataOffset = 24;  // sync (12) + header (4) + Form 1 subheader (8)
constexpr std::uint64_t kUserDataSize = 2048;
constexpr std::array<std::uint8_t, 12> kSync = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff,
                                                0xff, 0xff, 0xff, 0xff, 0xff, 0x00};

std::string upper(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

// Splits a cue line into words. Double quotes group a word that contains spaces and are removed.
std::vector<std::string> cue_words(std::string_view line) {
    std::vector<std::string> words;
    std::size_t i = 0;
    while (i < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        } else if (line[i] == '"') {
            std::size_t end = line.find('"', i + 1);
            if (end == std::string_view::npos) {
                end = line.size();
            }
            words.emplace_back(line.substr(i + 1, end - i - 1));
            i = end + 1;
        } else {
            std::size_t end = i;
            while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) {
                ++end;
            }
            words.emplace_back(line.substr(i, end - i));
            i = end;
        }
    }
    return words;
}

// The .bin named by the first track of the sheet. Only a MODE2/2352 track that starts at the first
// byte of the file is accepted: any other start would shift every LBA.
std::optional<std::filesystem::path> parse_cue(const std::filesystem::path& cue_path) {
    std::ifstream in(cue_path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::optional<std::string> file_name;
    bool track_seen = false;
    std::string line;
    for (bool first = true; std::getline(in, line); first = false) {
        if (first && line.starts_with("\xEF\xBB\xBF")) {
            line.erase(0, 3);
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::vector<std::string> words = cue_words(line);
        if (words.empty()) {
            continue;
        }
        const std::string command = upper(words[0]);
        if (command == "FILE") {
            if (file_name || words.size() < 3 || upper(words[2]) != "BINARY") {
                return std::nullopt;
            }
            file_name = words[1];
        } else if (command == "TRACK") {
            if (!file_name || track_seen || words.size() < 3 || upper(words[2]) != "MODE2/2352") {
                return std::nullopt;
            }
            track_seen = true;
        } else if (command == "INDEX" && track_seen && words.size() >= 3 &&
                   (words[1] == "01" || words[1] == "1")) {
            if (words[2] != "00:00:00") {
                return std::nullopt;
            }
            return cue_path.parent_path() / *file_name;
        }
    }
    return std::nullopt;
}

}  // namespace

Image::Image(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Image::~Image() = default;

std::unique_ptr<Image> Image::open(const std::filesystem::path& path) {
    const std::string ext = upper(path.extension().string());
    std::filesystem::path bin;
    if (ext == ".BIN") {
        bin = path;
    } else if (ext == ".CUE") {
        std::optional<std::filesystem::path> from_cue = parse_cue(path);
        if (!from_cue) {
            return nullptr;
        }
        bin = *from_cue;
    } else {
        return nullptr;
    }

    std::unique_ptr<ByteSource> source = open_file_source(bin);
    if (!source) {
        return nullptr;
    }
    const std::uint64_t size = source->size();
    if (size == 0 || size % kRawSectorSize != 0 ||
        size / kRawSectorSize > std::numeric_limits<std::uint32_t>::max()) {
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->sectors = static_cast<std::uint32_t>(size / kRawSectorSize);
    std::array<std::uint8_t, 12> sync{};
    impl->sync_ok = source->read_at(0, sync.data(), sync.size()) && sync == kSync;
    impl->source = std::move(source);
    return std::unique_ptr<Image>(new Image(std::move(impl)));
}

std::uint32_t Image::sector_count() const {
    return impl_->sectors;
}

bool Image::sync_ok() const {
    return impl_->sync_ok;
}

bool Image::read_user_data(std::uint32_t lba, void* dst) const {
    if (lba >= impl_->sectors) {
        return false;
    }
    const std::uint64_t offset = static_cast<std::uint64_t>(lba) * kRawSectorSize + kUserDataOffset;
    return impl_->source->read_at(offset, dst, kUserDataSize);
}

bool Image::read_raw(std::uint32_t lba, void* dst) const {
    if (lba >= impl_->sectors) {
        return false;
    }
    return impl_->source->read_at(static_cast<std::uint64_t>(lba) * kRawSectorSize, dst, kRawSectorSize);
}

}  // namespace sh::disc
