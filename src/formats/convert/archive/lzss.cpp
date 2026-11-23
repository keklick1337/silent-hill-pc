#include "formats/convert/archive/archive_converters.h"

#include <array>
#include <memory>

namespace sh::convert::archive {
namespace {

constexpr int N = 4096;       // ring buffer size
constexpr int F = 18;         // longest match
constexpr int THRESHOLD = 2;  // a match must be longer than this
constexpr int NIL = N;

// Okumura's encoder state (LZSS.C, 1989): a binary search tree per first byte over the ring buffer.
class Encoder {
public:
    std::vector<std::uint8_t> run(std::span<const std::uint8_t> src) {
        std::vector<std::uint8_t> out;
        const auto size = static_cast<std::uint32_t>(src.size());
        for (int i = 0; i < 4; ++i) {
            out.push_back(static_cast<std::uint8_t>(size >> (8 * i)));
        }
        text_.fill(0);
        for (int i = N + 1; i <= N + 256; ++i) {
            rson_[static_cast<std::size_t>(i)] = NIL;
        }
        for (int i = 0; i < N; ++i) {
            dad_[static_cast<std::size_t>(i)] = NIL;
        }
        std::array<std::uint8_t, 17> code{};
        std::size_t code_ptr = 1;
        std::uint8_t mask = 1;
        int s = 0, r = N - F, len = 0;
        std::size_t sp = 0;
        while (len < F && sp < src.size()) {
            text_[static_cast<std::size_t>(r + len)] = src[sp++];
            ++len;
        }
        if (len == 0) {
            return out;
        }
        for (int i = 1; i <= F; ++i) {
            insert(r - i);
        }
        insert(r);
        do {
            if (match_len_ > len) {
                match_len_ = len;
            }
            if (match_len_ <= THRESHOLD) {
                match_len_ = 1;
                code[0] |= mask;
                code[code_ptr++] = text_[static_cast<std::size_t>(r)];
            } else {
                code[code_ptr++] = static_cast<std::uint8_t>(match_pos_ & 0xFF);
                code[code_ptr++] =
                    static_cast<std::uint8_t>(((match_pos_ >> 4) & 0xF0) | (match_len_ - (THRESHOLD + 1)));
            }
            mask = static_cast<std::uint8_t>(mask << 1);
            if (mask == 0) {
                out.insert(out.end(), code.begin(), code.begin() + static_cast<std::ptrdiff_t>(code_ptr));
                code[0] = 0;
                code_ptr = 1;
                mask = 1;
            }
            const int last = match_len_;
            int i = 0;
            for (; i < last && sp < src.size(); ++i) {
                remove(s);
                const std::uint8_t c = src[sp++];
                text_[static_cast<std::size_t>(s)] = c;
                if (s < F - 1) {
                    text_[static_cast<std::size_t>(s + N)] = c;
                }
                s = (s + 1) & (N - 1);
                r = (r + 1) & (N - 1);
                insert(r);
            }
            while (i++ < last) {
                remove(s);
                s = (s + 1) & (N - 1);
                r = (r + 1) & (N - 1);
                if (--len) {
                    insert(r);
                }
            }
        } while (len > 0);
        if (code_ptr > 1) {
            out.insert(out.end(), code.begin(), code.begin() + static_cast<std::ptrdiff_t>(code_ptr));
        }
        return out;
    }

private:
    void insert(int r) {
        int cmp = 1;
        const std::size_t key = static_cast<std::size_t>(r);
        int p = N + 1 + text_[key];
        rson_[static_cast<std::size_t>(r)] = lson_[static_cast<std::size_t>(r)] = NIL;
        match_len_ = 0;
        for (;;) {
            if (cmp >= 0) {
                if (rson_[static_cast<std::size_t>(p)] != NIL) {
                    p = rson_[static_cast<std::size_t>(p)];
                } else {
                    rson_[static_cast<std::size_t>(p)] = r;
                    dad_[static_cast<std::size_t>(r)] = p;
                    return;
                }
            } else {
                if (lson_[static_cast<std::size_t>(p)] != NIL) {
                    p = lson_[static_cast<std::size_t>(p)];
                } else {
                    lson_[static_cast<std::size_t>(p)] = r;
                    dad_[static_cast<std::size_t>(r)] = p;
                    return;
                }
            }
            int i = 1;
            for (; i < F; ++i) {
                cmp = text_[key + static_cast<std::size_t>(i)] - text_[static_cast<std::size_t>(p + i)];
                if (cmp != 0) {
                    break;
                }
            }
            if (i > match_len_) {
                match_pos_ = p;
                match_len_ = i;
                if (match_len_ >= F) {
                    break;
                }
            }
        }
        const auto up = static_cast<std::size_t>(p);
        const auto ur = static_cast<std::size_t>(r);
        dad_[ur] = dad_[up];
        lson_[ur] = lson_[up];
        rson_[ur] = rson_[up];
        dad_[static_cast<std::size_t>(lson_[up])] = r;
        dad_[static_cast<std::size_t>(rson_[up])] = r;
        if (rson_[static_cast<std::size_t>(dad_[up])] == p) {
            rson_[static_cast<std::size_t>(dad_[up])] = r;
        } else {
            lson_[static_cast<std::size_t>(dad_[up])] = r;
        }
        dad_[up] = NIL;
    }

    void remove(int p) {
        const auto up = static_cast<std::size_t>(p);
        if (dad_[up] == NIL) {
            return;
        }
        int q;
        if (rson_[up] == NIL) {
            q = lson_[up];
        } else if (lson_[up] == NIL) {
            q = rson_[up];
        } else {
            q = lson_[up];
            if (rson_[static_cast<std::size_t>(q)] != NIL) {
                do {
                    q = rson_[static_cast<std::size_t>(q)];
                } while (rson_[static_cast<std::size_t>(q)] != NIL);
                const auto uq = static_cast<std::size_t>(q);
                rson_[static_cast<std::size_t>(dad_[uq])] = lson_[uq];
                dad_[static_cast<std::size_t>(lson_[uq])] = dad_[uq];
                lson_[uq] = lson_[up];
                dad_[static_cast<std::size_t>(lson_[up])] = q;
            }
            rson_[static_cast<std::size_t>(q)] = rson_[up];
            dad_[static_cast<std::size_t>(rson_[up])] = q;
        }
        dad_[static_cast<std::size_t>(q)] = dad_[up];
        if (rson_[static_cast<std::size_t>(dad_[up])] == p) {
            rson_[static_cast<std::size_t>(dad_[up])] = q;
        } else {
            lson_[static_cast<std::size_t>(dad_[up])] = q;
        }
        dad_[up] = NIL;
    }

    std::array<std::uint8_t, N + F - 1> text_{};
    std::array<int, N + 1> lson_{};
    std::array<int, N + 257> rson_{};
    std::array<int, N + 1> dad_{};
    int match_pos_ = 0;
    int match_len_ = 0;
};

}  // namespace

Result<std::vector<std::uint8_t>> lzss_decode(std::span<const std::uint8_t> packed, std::size_t* consumed) {
    using Out = Result<std::vector<std::uint8_t>>;
    if (packed.size() < 4) {
        return Out::fail("LZSS: shorter than the size header");
    }
    const std::uint32_t size = static_cast<std::uint32_t>(packed[0]) | (static_cast<std::uint32_t>(packed[1]) << 8) |
                               (static_cast<std::uint32_t>(packed[2]) << 16) |
                               (static_cast<std::uint32_t>(packed[3]) << 24);
    if (size > 64u * 1024 * 1024) {
        return Out::fail("LZSS: unpacked size implausible");
    }
    std::vector<std::uint8_t> out;
    out.reserve(size);
    std::array<std::uint8_t, N> window{};
    int r = N - F;
    unsigned flags = 0;
    std::size_t i = 4;
    while (i < packed.size() && out.size() < size) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            flags = packed[i++] | 0xFF00u;
        }
        if (flags & 1) {
            if (i >= packed.size()) {
                break;
            }
            const std::uint8_t c = packed[i++];
            out.push_back(c);
            window[static_cast<std::size_t>(r)] = c;
            r = (r + 1) & (N - 1);
        } else {
            if (i + 1 >= packed.size()) {
                break;
            }
            const int offset = packed[i] | ((packed[i + 1] & 0xF0) << 4);
            const int length = (packed[i + 1] & 0x0F) + THRESHOLD + 1;
            i += 2;
            for (int k = 0; k < length; ++k) {
                const std::uint8_t c = window[static_cast<std::size_t>((offset + k) & (N - 1))];
                out.push_back(c);
                window[static_cast<std::size_t>(r)] = c;
                r = (r + 1) & (N - 1);
            }
        }
    }
    if (out.size() < size) {
        return Out::fail("LZSS: the stream ends before the unpacked size");
    }
    out.resize(size);
    if (consumed) {
        *consumed = i;
    }
    return Out::ok(std::move(out));
}

std::vector<std::uint8_t> lzss_encode(std::span<const std::uint8_t> data) {
    // The tree arrays are about 50 KiB: on the heap, not the stack of an import thread.
    auto encoder = std::make_unique<Encoder>();
    return encoder->run(data);
}

}  // namespace sh::convert::archive
