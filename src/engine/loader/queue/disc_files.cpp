// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/loader/queue/disc_files.h"

#include <cctype>
#include <utility>

namespace sh::loader {

namespace ft = convert::filetable;

namespace {

constexpr int kNameChars = 8;
constexpr int kCharsPerPart = 4;
constexpr int kCharBits = 6;
constexpr int kCharOffset = 0x20;

const std::string kEmpty;

std::uint32_t load_le32(const std::uint8_t* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) |
           (std::uint32_t{p[3]} << 24);
}

}  // namespace

void DiscFiles::assign(std::vector<ft::Record> records) {
    files_.clear();
    files_.reserve(records.size());
    for (ft::Record& r : records) {
        Entry e;
        e.record = r;
        e.path = ft::path_of(r);
        files_.push_back(std::move(e));
    }
    sectors_before_remap_.clear();
    index_paths();
}

std::string DiscFiles::key_of(std::string_view path) {
    std::string key;
    key.reserve(path.size());
    for (const char ch : path) {
        const char c = ch == '\\' ? '/' : static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (c == '/' && key.empty()) {
            continue;
        }
        key += c;
    }
    return key;
}

void DiscFiles::index_paths() {
    by_path_.clear();
    by_path_.reserve(files_.size());
    for (std::size_t i = 0; i < files_.size(); ++i) {
        by_path_.emplace(files_[i].path, static_cast<int>(i));  // the first of equal paths wins
    }
}

int DiscFiles::find(std::string_view path) const {
    const auto it = by_path_.find(key_of(path));
    return it != by_path_.end() ? it->second : kNone;
}

const std::string& DiscFiles::path(int file) const {
    return valid(file) ? files_[static_cast<std::size_t>(file)].path : kEmpty;
}

std::string DiscFiles::name(int file) const {
    const std::string& p = path(file);
    const std::size_t slash = p.find('/');
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

std::string DiscFiles::directory(int file) const {
    const std::string& p = path(file);
    const std::size_t slash = p.find('/');
    return slash == std::string::npos ? std::string() : p.substr(0, slash);
}

std::uint32_t DiscFiles::first_sector(int file) const {
    return valid(file) ? files_[static_cast<std::size_t>(file)].record.sector : 0;
}

std::uint32_t DiscFiles::bytes(int file) const {
    return valid(file) ? files_[static_cast<std::size_t>(file)].record.blocks * kBlockBytes : 0;
}

std::uint32_t DiscFiles::sector_bytes(int file) const {
    return (bytes(file) + kSectorBytes - 1) & ~(kSectorBytes - 1);
}

int DiscFiles::type(int file) const {
    return valid(file) ? files_[static_cast<std::size_t>(file)].record.type : ft::kNoType;
}

const ft::Record& DiscFiles::record(int file) const {
    static const ft::Record kNoRecord;
    return valid(file) ? files_[static_cast<std::size_t>(file)].record : kNoRecord;
}

int DiscFiles::next_of_type(int wanted, int start, int direction) const {
    const int step = direction < 0 ? -1 : 1;
    const std::uint32_t n = static_cast<std::uint32_t>(count());
    // Unsigned on purpose: stepping below 0 wraps past the end and lands on the last file.
    std::uint32_t at = static_cast<std::uint32_t>(start + step);
    for (std::uint32_t lap = 0; lap < n; ++lap) {
        if (at >= n) {
            at = direction < 0 ? n - 1 : 0;
        }
        if (files_[at].record.type == static_cast<std::uint32_t>(wanted)) {
            return static_cast<int>(at);
        }
        at += static_cast<std::uint32_t>(step);
    }
    return kNone;
}

int DiscFiles::next_named(const char* name, int wanted, int start) const {
    // Encoded exactly as the table stores names, without masking: a character outside 0x20..0x5F
    // spills into its neighbour and then matches nothing, as in the original search.
    std::int32_t part[2] = {0, 0};
    int shift = 0;
    for (int i = 0; i < kNameChars; ++i) {
        const std::int32_t c = name[i];
        if (i == kCharsPerPart) {
            shift = 0;
        }
        if (c == '\0' || c == '.') {
            break;
        }
        part[i < kCharsPerPart ? 0 : 1] |= static_cast<std::int32_t>(static_cast<std::uint32_t>(c - kCharOffset) << shift);
        shift += kCharBits;
    }
    for (int i = start < 0 ? 0 : start; i < count(); ++i) {
        const ft::Record& r = files_[static_cast<std::size_t>(i)].record;
        if (static_cast<std::int32_t>(r.name4567) == part[1] && static_cast<std::int32_t>(r.name0123) == part[0] &&
            r.type == static_cast<std::uint32_t>(wanted)) {
            return i;
        }
    }
    return kNone;
}

ft::Record DiscFiles::unpack(const std::uint8_t* raw) {
    const std::uint32_t w0 = load_le32(raw);
    const std::uint32_t w1 = load_le32(raw + 4);
    const std::uint32_t w2 = load_le32(raw + 8);
    ft::Record r;
    r.sector = w0 & 0x7FFFFu;
    r.blocks = (w0 >> 19) & 0xFFFu;
    r.dir = static_cast<std::uint8_t>(w1 & 0xFu);
    r.name0123 = (w1 >> 4) & 0xFFFFFFu;
    r.name4567 = w2 & 0xFFFFFFu;
    r.type = static_cast<std::uint8_t>((w2 >> 24) & 0xFu);
    return r;
}

int DiscFiles::remap(std::span<const std::uint8_t> raw_entries) {
    const std::size_t n = raw_entries.size() / kRawEntryBytes;
    if (n == 0) {
        return 0;
    }
    std::vector<ft::Record> disc;
    disc.reserve(n);
    for (std::size_t j = 0; j < n; ++j) {
        disc.push_back(unpack(raw_entries.data() + j * kRawEntryBytes));
    }

    std::vector<std::uint32_t> before;
    before.reserve(files_.size());
    int moved = 0;
    for (Entry& e : files_) {
        ft::Record& g = e.record;
        before.push_back(g.sector);
        for (const ft::Record& d : disc) {
            if (d.name0123 == g.name0123 && d.name4567 == g.name4567 && d.type == g.type && d.dir == g.dir) {
                if (g.sector != d.sector || g.blocks != d.blocks) {
                    g.sector = d.sector;
                    g.blocks = d.blocks;
                    ++moved;
                }
                break;
            }
        }
    }
    if (moved != 0) {
        sectors_before_remap_ = std::move(before);
    }
    return moved;
}

std::uint32_t DiscFiles::moved_sector(std::uint32_t old_sector) const {
    for (std::size_t i = 0; i < sectors_before_remap_.size(); ++i) {
        if (sectors_before_remap_[i] == old_sector) {
            return files_[i].record.sector;
        }
    }
    return old_sector;
}

DiscFiles& disc_files() {
    static DiscFiles table;
    return table;
}

}  // namespace sh::loader
