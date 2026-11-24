#include "formats/disc/zip_archive.h"

#include "miniz/include/miniz.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <list>
#include <mutex>
#include <utility>

namespace sh::disc {

namespace {

constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50;
constexpr std::uint64_t kLocalHeaderSize = 30;
constexpr std::uint16_t kMethodStored = 0;
constexpr std::uint16_t kMethodDeflated = 8;

// A restart point every 2 MiB costs about 43 KiB (inflater state and window): some 15 MiB for a CD image. The span
// is a multiple of the window, so the inflater, which always fills the window to its end, stops exactly on it.
constexpr std::uint64_t kSpan = 2u << 20;
constexpr std::size_t kCachedSpans = 16;
constexpr std::size_t kInputChunk = 64u << 10;
constexpr std::size_t kWindow = TINFL_LZ_DICT_SIZE;
static_assert(kSpan % kWindow == 0);

std::uint32_t le16(const std::uint8_t* p) { return static_cast<std::uint32_t>(p[0] | (p[1] << 8)); }

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// A stored member: a window of the archive file.
class RangeSource final : public ByteSource {
public:
    RangeSource(std::shared_ptr<const ByteSource> file, std::uint64_t offset, std::uint64_t size)
        : file_(std::move(file)), offset_(offset), size_(size) {}

    std::uint64_t size() const override { return size_; }

    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > size_ || count > size_ - offset) {
            return false;
        }
        return file_->read_at(offset_ + offset, dst, count);
    }

private:
    std::shared_ptr<const ByteSource> file_;
    std::uint64_t offset_;
    std::uint64_t size_;
};

// A deflated member. The inflater's state is a plain struct and its window the last 32 KiB of output, so a copy of
// both taken between two calls resumes the stream from that point.
class InflateSource final : public ByteSource {
public:
    InflateSource(std::shared_ptr<const ByteSource> file, std::uint64_t offset, std::uint64_t packed,
                  std::uint64_t size)
        : file_(std::move(file)), offset_(offset), packed_(packed), size_(size) {
        auto start = std::make_shared<Restart>();
        tinfl_init(&start->state);
        restarts_.push_back(std::move(start));
    }

    std::uint64_t size() const override { return size_; }

    bool read_at(std::uint64_t offset, void* dst, std::size_t count) const override {
        if (offset > size_ || count > size_ - offset) {
            return false;
        }
        auto* out = static_cast<std::uint8_t*>(dst);
        while (count > 0) {
            const std::uint64_t index = offset / kSpan;
            const std::shared_ptr<const Span> span = span_at(index);
            if (!span) {
                return false;
            }
            const std::uint64_t within = offset - index * kSpan;
            const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(count, span->size() - within));
            if (n == 0) {
                return false;
            }
            std::memcpy(out, span->data() + within, n);
            out += n;
            offset += n;
            count -= n;
        }
        return true;
    }

private:
    using Span = std::vector<std::uint8_t>;

    struct Restart {
        tinfl_decompressor state{};
        std::array<std::uint8_t, kWindow> window{};
        std::uint64_t packed_pos = 0;  // compressed bytes the state has consumed
    };

    // Inflates span `index` from its restart point; on success also returns the restart point of the next span.
    bool inflate_span(std::uint64_t index, Restart from, Span& out, std::shared_ptr<Restart>& next) const {
        const std::uint64_t begin = index * kSpan;
        const std::uint64_t end = std::min(size_, begin + kSpan);
        out.resize(static_cast<std::size_t>(end - begin));
        std::vector<std::uint8_t> input(kInputChunk);
        std::size_t in_at = 0;
        std::size_t in_left = 0;
        std::uint64_t packed_pos = from.packed_pos;
        std::uint64_t produced = begin;
        while (produced < end) {
            if (in_left == 0 && packed_pos < packed_) {
                const std::size_t n =
                    static_cast<std::size_t>(std::min<std::uint64_t>(input.size(), packed_ - packed_pos));
                if (!file_->read_at(offset_ + packed_pos, input.data(), n)) {
                    return false;
                }
                in_at = 0;
                in_left = n;
            }
            const std::size_t window_at = static_cast<std::size_t>(produced % kWindow);
            std::size_t in_size = in_left;
            std::size_t out_size = kWindow - window_at;
            const bool more = packed_pos + in_left < packed_;
            const tinfl_status status =
                tinfl_decompress(&from.state, input.data() + in_at, &in_size, from.window.data(),
                                 from.window.data() + window_at, &out_size, more ? TINFL_FLAG_HAS_MORE_INPUT : 0);
            in_at += in_size;
            in_left -= in_size;
            packed_pos += in_size;
            if (out_size > end - produced) {
                return false;  // more output than the member's size: a damaged archive
            }
            std::memcpy(out.data() + (produced - begin), from.window.data() + window_at, out_size);
            produced += out_size;
            if (status == TINFL_STATUS_DONE) {
                break;
            }
            if (status < TINFL_STATUS_DONE || (status == TINFL_STATUS_NEEDS_MORE_INPUT && !more && in_left == 0)) {
                return false;
            }
        }
        if (produced != end) {
            return false;
        }
        from.packed_pos = packed_pos;
        next = std::make_shared<Restart>(std::move(from));
        return true;
    }

    std::shared_ptr<const Span> cached(std::uint64_t index) const {
        for (auto it = cache_.begin(); it != cache_.end(); ++it) {
            if (it->first == index) {
                cache_.splice(cache_.begin(), cache_, it);
                return it->second;
            }
        }
        return nullptr;
    }

    // Caller holds mutex_.
    void remember(std::uint64_t index, std::shared_ptr<const Span> span, std::shared_ptr<Restart> next) const {
        if (index + 1 == restarts_.size() && index + 1 < (size_ + kSpan - 1) / kSpan) {
            restarts_.push_back(std::move(next));
        }
        if (!cached(index)) {
            cache_.emplace_front(index, std::move(span));
            if (cache_.size() > kCachedSpans) {
                cache_.pop_back();
            }
        }
    }

    std::shared_ptr<const Span> decode(std::uint64_t index) const {
        std::shared_ptr<const Restart> from;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto hit = cached(index)) {
                return hit;
            }
            from = restarts_[index];
        }
        auto span = std::make_shared<Span>();
        std::shared_ptr<Restart> next;
        if (!inflate_span(index, *from, *span, next)) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        remember(index, span, std::move(next));
        return span;
    }

    std::shared_ptr<const Span> span_at(std::uint64_t index) const {
        bool known = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (auto hit = cached(index)) {
                return hit;
            }
            known = index < restarts_.size();
        }
        if (known) {
            return decode(index);  // inflated outside the lock: threads reading different spans do not wait
        }
        // Restart points are made in order: the spans before this one are read through once, by one thread.
        std::lock_guard<std::mutex> forward(forward_mutex_);
        std::uint64_t first = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            first = restarts_.size() - 1;
        }
        std::shared_ptr<const Span> span;
        for (std::uint64_t i = std::min(first, index); i <= index; ++i) {
            span = decode(i);
            if (!span) {
                return nullptr;
            }
        }
        return span;
    }

    std::shared_ptr<const ByteSource> file_;
    std::uint64_t offset_;
    std::uint64_t packed_;
    std::uint64_t size_;
    mutable std::mutex mutex_;          // restarts_ and cache_
    mutable std::mutex forward_mutex_;  // the first read-through
    mutable std::vector<std::shared_ptr<const Restart>> restarts_;
    mutable std::list<std::pair<std::uint64_t, std::shared_ptr<const Span>>> cache_;
};

std::size_t read_callback(void* opaque, mz_uint64 offset, void* dst, std::size_t count) {
    const auto* file = static_cast<const ByteSource*>(opaque);
    const std::uint64_t size = file->size();
    if (offset >= size) {
        return 0;
    }
    count = static_cast<std::size_t>(std::min<std::uint64_t>(count, size - offset));
    return file->read_at(offset, dst, count) ? count : 0;
}

}  // namespace

struct ZipArchive::Impl {
    std::shared_ptr<const ByteSource> file;
    std::vector<ZipMember> members;
    std::vector<std::uint64_t> header_offsets;
    std::vector<std::uint64_t> packed_sizes;
    std::vector<std::uint16_t> methods;
};

ZipArchive::ZipArchive(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

ZipArchive::~ZipArchive() = default;

std::unique_ptr<ZipArchive> ZipArchive::open(const std::filesystem::path& path) {
    std::shared_ptr<const ByteSource> file = open_file_source(path);
    return file ? open(std::move(file)) : nullptr;
}

std::unique_ptr<ZipArchive> ZipArchive::open(std::shared_ptr<const ByteSource> file) {
    if (!file) {
        return nullptr;
    }
    // Reads go through the source (64-bit offsets on every platform), not through miniz's stdio.
    mz_zip_archive zip{};
    zip.m_pRead = read_callback;
    zip.m_pIO_opaque = const_cast<ByteSource*>(file.get());
    if (!mz_zip_reader_init(&zip, file->size(), MZ_ZIP_FLAG_DO_NOT_SORT_CENTRAL_DIRECTORY)) {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->file = file;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            continue;
        }
        ZipMember member;
        member.name = stat.m_filename;
        member.size = stat.m_uncomp_size;
        member.directory = stat.m_is_directory != 0;
        member.readable = !member.directory && !stat.m_is_encrypted &&
                          (stat.m_method == kMethodStored || stat.m_method == kMethodDeflated) &&
                          (stat.m_method != kMethodStored || stat.m_comp_size == stat.m_uncomp_size);
        impl->members.push_back(std::move(member));
        impl->header_offsets.push_back(stat.m_local_header_ofs);
        impl->packed_sizes.push_back(stat.m_comp_size);
        impl->methods.push_back(static_cast<std::uint16_t>(stat.m_method));
    }
    mz_zip_reader_end(&zip);
    return std::unique_ptr<ZipArchive>(new ZipArchive(std::move(impl)));
}

const std::vector<ZipMember>& ZipArchive::members() const { return impl_->members; }

std::optional<std::size_t> ZipArchive::find(std::string_view name) const {
    for (std::size_t i = 0; i < impl_->members.size(); ++i) {
        if (same_name(impl_->members[i].name, name)) {
            return i;
        }
    }
    return std::nullopt;
}

std::unique_ptr<ByteSource> ZipArchive::open_member(std::size_t index) const {
    if (index >= impl_->members.size() || !impl_->members[index].readable) {
        return nullptr;
    }
    // The data starts after the local header, whose name and extra field may differ in length from the central
    // directory's copy.
    std::array<std::uint8_t, kLocalHeaderSize> header{};
    const std::uint64_t at = impl_->header_offsets[index];
    if (!impl_->file->read_at(at, header.data(), header.size()) || le32(header.data()) != kLocalHeaderSignature) {
        return nullptr;
    }
    const std::uint64_t data = at + kLocalHeaderSize + le16(header.data() + 26) + le16(header.data() + 28);
    const std::uint64_t packed = impl_->packed_sizes[index];
    const std::uint64_t size = impl_->members[index].size;
    if (data > impl_->file->size() || packed > impl_->file->size() - data) {
        return nullptr;
    }
    if (impl_->methods[index] == kMethodStored) {
        return std::make_unique<RangeSource>(impl_->file, data, size);
    }
    return std::make_unique<InflateSource>(impl_->file, data, packed, size);
}

bool is_zip_path(const std::filesystem::path& path) { return same_name(path.extension().string(), ".zip"); }

}  // namespace sh::disc
