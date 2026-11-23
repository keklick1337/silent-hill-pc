// SPDX-License-Identifier: GPL-3.0-or-later
#include "media/fmv/codec/avi_file.h"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <utility>

namespace sh::fmv {

namespace {

constexpr std::uint64_t kChunkHeadBytes = 8;   // id and size
constexpr std::uint64_t kListHeadBytes = 12;   // id, size and the list's type
constexpr std::uint64_t kMainHeaderBytes = 56;  // the payload of `avih`
constexpr std::uint32_t kFormatExtensible = 0xFFFE;

// Little-endian fields of a file; what lies past its end reads as zero.
class Fields {
public:
    explicit Fields(const ByteSource& source) : source_(source), size_(source.size()) {}

    std::uint64_t size() const { return size_; }
    bool has(std::uint64_t offset, std::uint64_t count) const { return offset <= size_ && count <= size_ - offset; }

    std::uint32_t u16(std::uint64_t offset) const {
        std::uint8_t b[2] = {};
        if (has(offset, 2)) source_.read_at(offset, b, 2);
        return static_cast<std::uint32_t>(b[0]) | static_cast<std::uint32_t>(b[1]) << 8;
    }
    std::uint32_t u32(std::uint64_t offset) const { return u16(offset) | u16(offset + 2) << 16; }
    std::array<char, 4> tag(std::uint64_t offset) const {
        std::array<char, 4> t{};
        if (has(offset, 4)) source_.read_at(offset, t.data(), 4);
        return t;
    }

private:
    const ByteSource& source_;
    std::uint64_t size_;
};

bool is(const std::array<char, 4>& tag, std::string_view name) { return std::memcmp(tag.data(), name.data(), 4) == 0; }

std::uint64_t even(std::uint64_t offset) { return offset + (offset & 1); }

bool digit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

Result<AviFile> AviFile::open(std::unique_ptr<ByteSource> source) {
    using R = Result<AviFile>;
    if (!source) return R::fail("the file cannot be opened");
    AviFile file;
    file.source_ = std::move(source);
    const Fields f(*file.source_);

    // The segments' sizes are 32-bit, their places in the file are not.
    std::uint64_t at = 0;
    bool first = true;
    while (f.has(at, kListHeadBytes)) {
        if (!is(f.tag(at), "RIFF") || !is(f.tag(at + 8), first ? "AVI " : "AVIX")) break;
        // A writer that was cut off leaves a size that runs past the file.
        const std::uint64_t end = std::min(at + kChunkHeadBytes + f.u32(at + 4), f.size());
        file.scan_segment(at + kListHeadBytes, end, first);
        first = false;
        at = even(end);
    }
    if (first) return R::fail("not an AVI file");
    if (file.chunks_.empty()) return R::fail("no picture or sound data in the file");
    return R::ok(std::move(file));
}

// The headers are only in the first segment; every segment may hold movie data.
void AviFile::scan_segment(std::uint64_t begin, std::uint64_t end, bool first) {
    const Fields f(*source_);
    std::uint64_t at = begin;
    while (at + kChunkHeadBytes <= end) {
        const std::uint32_t size = f.u32(at + 4);
        const bool list = is(f.tag(at), "LIST");
        std::uint64_t next = at + kChunkHeadBytes + size;
        if (list) {
            if (!f.has(at, kListHeadBytes)) break;
            const std::array<char, 4> type = f.tag(at + 8);
            if (first && is(type, "hdrl")) scan_headers(at + kListHeadBytes, next);
            if (is(type, "movi")) scan_movie_data(at + kListHeadBytes, next);
        } else if (size == 0) {
            break;  // damaged: nothing says where the next chunk is
        }
        next = even(next);
        if (next > end) break;
        at = next;
    }
}

void AviFile::scan_headers(std::uint64_t begin, std::uint64_t end) {
    const Fields f(*source_);
    // The list opens with the main header.
    microseconds_per_frame_ = f.u32(begin + kChunkHeadBytes);
    declared_frames_ = f.u32(begin + kChunkHeadBytes + 16);
    std::uint64_t at = begin + kChunkHeadBytes + kMainHeaderBytes;
    // Then one list per stream, with padding and OpenDML lists among them.
    while (at < end && f.has(at, kListHeadBytes)) {
        const std::uint64_t next = at + kChunkHeadBytes + f.u32(at + 4);
        if (is(f.tag(at), "LIST")) scan_stream_list(at + kListHeadBytes, next);
        at = next;
    }
}

// A stream's header chunk says what the format chunk after it describes. When several streams of a kind exist
// the last one's format stands.
void AviFile::scan_stream_list(std::uint64_t begin, std::uint64_t end) {
    const Fields f(*source_);
    enum class Stream { Picture, Sound, Other } stream = Stream::Picture;
    std::uint64_t at = begin;
    while (at < end && f.has(at, kChunkHeadBytes)) {
        const std::array<char, 4> id = f.tag(at);
        const std::uint32_t size = f.u32(at + 4);
        const std::uint64_t body = at + kChunkHeadBytes;
        if (is(id, "strh")) {
            const std::array<char, 4> type = f.tag(body);
            stream = is(type, "vids") ? Stream::Picture : is(type, "auds") ? Stream::Sound : Stream::Other;
        } else if (is(id, "strf") && stream == Stream::Picture) {
            video_.width = static_cast<std::int32_t>(f.u32(body + 4));
            video_.height = static_cast<std::int32_t>(f.u32(body + 8));
            video_.bits_per_pixel = static_cast<int>(f.u16(body + 14));
            video_.fourcc = f.tag(body + 16);
        } else if (is(id, "strf") && stream == Stream::Sound) {
            std::uint32_t format = f.u16(body);
            // The extension is a size word, the valid bits, the speaker mask and the subformat GUID, whose first
            // word is the real format tag.
            if (format == kFormatExtensible && size >= 26) format = f.u16(body + 24);
            audio_.format_tag = static_cast<int>(format);
            audio_.channels = static_cast<int>(f.u16(body + 2));
            audio_.sample_rate = static_cast<std::int32_t>(f.u32(body + 4));
            audio_.bits_per_sample = static_cast<int>(f.u16(body + 14));
        }
        at = even(body + size);
    }
}

// Data chunks are named by two digits of the stream number and `db` or `dc` for a picture, `wb` for sound.
// `rec ` lists only group chunks, so their contents are read as part of the run; index chunks, padding and palette
// changes are stepped over.
void AviFile::scan_movie_data(std::uint64_t begin, std::uint64_t end) {
    const Fields f(*source_);
    std::uint64_t at = begin;
    while (at + kChunkHeadBytes <= end && f.has(at, kChunkHeadBytes)) {
        const std::array<char, 4> id = f.tag(at);
        if (is(id, "LIST")) {
            at += kListHeadBytes;
            continue;
        }
        const std::uint32_t size = f.u32(at + 4);
        if (size > 0 && digit(id[0]) && digit(id[1])) {
            const std::uint64_t body = at + kChunkHeadBytes;
            if (id[2] == 'd' && (id[3] == 'b' || id[3] == 'c')) chunks_.push_back({ChunkKind::Picture, size, body});
            if (id[2] == 'w' && id[3] == 'b') chunks_.push_back({ChunkKind::Sound, size, body});
        }
        at = even(at + kChunkHeadBytes + size);
    }
}

void AviFile::read(const Chunk& chunk, std::vector<std::uint8_t>& out) const {
    out.assign(chunk.size, 0);
    const std::uint64_t file_size = source_->size();
    if (chunk.offset >= file_size) return;
    const std::uint64_t present = std::min<std::uint64_t>(chunk.size, file_size - chunk.offset);
    source_->read_at(chunk.offset, out.data(), static_cast<std::size_t>(present));
}

}  // namespace sh::fmv
