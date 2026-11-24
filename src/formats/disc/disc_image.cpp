#include "formats/disc/disc_image.h"

#include "formats/common/byte_source.h"
#include "formats/disc/zip_archive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
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

// The file named by the first track of the sheet. Only a MODE2/2352 track that starts at the first
// byte of the file is accepted: any other start would shift every LBA.
std::optional<std::string> parse_cue(std::istream& in) {
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
            return file_name;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> parse_cue(const std::filesystem::path& cue_path) {
    std::ifstream in(cue_path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    const std::optional<std::string> name = parse_cue(in);
    if (!name) {
        return std::nullopt;
    }
    return cue_path.parent_path() / *name;
}

std::string extension_of(const std::string& member) {
    const std::size_t slash = member.find_last_of('/');
    const std::size_t dot = member.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    return upper(member.substr(dot));
}

std::string base_name(const std::string& member) {
    const std::size_t slash = member.find_last_of('/');
    return slash == std::string::npos ? member : member.substr(slash + 1);
}

// The member a cue sheet's FILE line names: beside the sheet first, then the only member of that name anywhere.
std::optional<std::size_t> cue_target(const ZipArchive& zip, const std::string& cue, std::string file) {
    std::replace(file.begin(), file.end(), '\\', '/');
    const std::size_t slash = cue.find_last_of('/');
    const std::string dir = slash == std::string::npos ? std::string() : cue.substr(0, slash + 1);
    if (auto found = zip.find(dir + file)) {
        return found;
    }
    std::optional<std::size_t> only;
    const std::string wanted = upper(base_name(file));
    const auto& members = zip.members();
    for (std::size_t i = 0; i < members.size(); ++i) {
        if (!members[i].directory && upper(base_name(members[i].name)) == wanted) {
            if (only) {
                return std::nullopt;
            }
            only = i;
        }
    }
    return only;
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
    } else if (ext == ".ZIP") {
        ZipImage zipped = open_zip_image(path);
        return zipped.ok() ? open(std::move(zipped.source)) : nullptr;
    } else {
        return nullptr;
    }
    return open(open_file_source(bin));
}

std::unique_ptr<Image> Image::open(std::unique_ptr<ByteSource> source) {
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

ZipImage open_zip_image(const std::filesystem::path& zip_path) {
    ZipImage result;
    const std::unique_ptr<ZipArchive> zip = ZipArchive::open(zip_path);
    if (!zip) {
        return result;
    }
    const auto& members = zip->members();
    std::vector<std::size_t> by_name(members.size());
    for (std::size_t i = 0; i < by_name.size(); ++i) {
        by_name[i] = i;
    }
    std::sort(by_name.begin(), by_name.end(),
              [&](std::size_t a, std::size_t b) { return members[a].name < members[b].name; });

    const auto take = [&](std::size_t index, std::string cue) {
        std::unique_ptr<ByteSource> source = zip->open_member(index);
        if (!source) {
            return false;
        }
        result.status = ZipImage::Status::Ok;
        result.cue = std::move(cue);
        result.member = members[index].name;
        result.size = members[index].size;
        result.source = std::move(source);
        return true;
    };

    // A cue sheet is text of a few hundred bytes; anything far larger is not one.
    constexpr std::uint64_t kMaxCueBytes = 1u << 16;
    ZipImage::Status cue_problem = ZipImage::Status::NoImage;
    for (const std::size_t i : by_name) {
        if (members[i].directory || extension_of(members[i].name) != ".CUE") {
            continue;
        }
        std::string text(static_cast<std::size_t>(std::min(members[i].size, kMaxCueBytes)), '\0');
        const std::unique_ptr<ByteSource> sheet = zip->open_member(i);
        if (!sheet || members[i].size > kMaxCueBytes || !sheet->read_at(0, text.data(), text.size())) {
            cue_problem = ZipImage::Status::CueNoFile;
            continue;
        }
        std::istringstream in(text);
        const std::optional<std::string> file = parse_cue(in);
        if (!file) {
            cue_problem = ZipImage::Status::CueNoFile;
            continue;
        }
        const std::optional<std::size_t> target = cue_target(*zip, members[i].name, *file);
        if (target && take(*target, members[i].name)) {
            return result;
        }
        cue_problem = ZipImage::Status::CueTargetMissing;
        result.cue_target = *file;
    }
    for (const char* ext : {".BIN", ".ISO", ".IMG"}) {
        for (const std::size_t i : by_name) {
            if (members[i].readable && extension_of(members[i].name) == ext && take(i, {})) {
                return result;
            }
        }
    }
    result.status = cue_problem;
    return result;
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
