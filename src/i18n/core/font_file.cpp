// SPDX-License-Identifier: GPL-3.0-or-later
#include "i18n/core/font_file.h"

#include "i18n/core/unicode.h"

#include <algorithm>
#include <fstream>
#include <functional>
#include <optional>

namespace sh::i18n {
namespace {

constexpr std::uint32_t tag(char a, char b, char c, char d) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

constexpr std::uint32_t kCollection = tag('t', 't', 'c', 'f');
constexpr std::uint32_t kTrueType = 0x00010000;
constexpr std::uint32_t kOpenType = tag('O', 'T', 'T', 'O');
constexpr std::uint32_t kAppleTrueType = tag('t', 'r', 'u', 'e');
constexpr std::size_t kMaxFaces = 64;
constexpr std::size_t kMaxTables = 128;
constexpr std::size_t kMaxTableBytes = 8u << 20;  // a character map or name table larger than this is not one

// Reads `size` bytes at `offset` of the font; empty when they are not there.
using Reader = std::function<std::vector<std::uint8_t>(std::uint32_t offset, std::size_t size)>;

std::uint32_t be16(const std::uint8_t* p) { return (static_cast<std::uint32_t>(p[0]) << 8) | p[1]; }
std::uint32_t be32(const std::uint8_t* p) { return (be16(p) << 16) | be16(p + 2); }

struct Table {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
};

struct Directory {
    Table name, os2, post, cmap;
};

std::optional<Directory> read_directory(const Reader& read, std::uint32_t at) {
    const std::vector<std::uint8_t> head = read(at, 12);
    if (head.size() < 12) {
        return std::nullopt;
    }
    const std::uint32_t version = be32(head.data());
    if (version != kTrueType && version != kOpenType && version != kAppleTrueType) {
        return std::nullopt;
    }
    const std::size_t count = be16(head.data() + 4);
    if (count == 0 || count > kMaxTables) {
        return std::nullopt;
    }
    const std::vector<std::uint8_t> records = read(at + 12, count * 16);
    if (records.size() < count * 16) {
        return std::nullopt;
    }
    Directory dir;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* r = records.data() + i * 16;
        const Table t{be32(r + 8), be32(r + 12)};
        switch (be32(r)) {
            case tag('n', 'a', 'm', 'e'): dir.name = t; break;
            case tag('O', 'S', '/', '2'): dir.os2 = t; break;
            case tag('p', 'o', 's', 't'): dir.post = t; break;
            case tag('c', 'm', 'a', 'p'): dir.cmap = t; break;
            default: break;
        }
    }
    return dir;
}

std::vector<std::uint8_t> read_table(const Reader& read, const Table& t, std::size_t at_most = kMaxTableBytes) {
    if (t.length == 0) {
        return {};
    }
    return read(t.offset, std::min<std::size_t>(t.length, at_most));
}

std::string name_string(const std::uint8_t* p, std::size_t length, bool utf16) {
    std::string out;
    if (!utf16) {
        for (std::size_t i = 0; i < length; ++i) {
            append_utf8(out, p[i]);  // Mac Roman's ASCII half is all family names use
        }
        return out;
    }
    for (std::size_t i = 0; i + 1 < length; i += 2) {
        char32_t cp = be16(p + i);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < length) {
            const char32_t low = be16(p + i + 2);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        append_utf8(out, cp);
    }
    return out;
}

void read_names(const std::vector<std::uint8_t>& t, FontFace& face) {
    if (t.size() < 6) {
        return;
    }
    const std::size_t count = be16(t.data() + 2);
    const std::size_t storage = be16(t.data() + 4);
    // Rank of a record: the typographic names over the legacy ones, Windows English over any other encoding.
    int family_rank = 0;
    int style_rank = 0;
    for (std::size_t i = 0; i < count && 6 + (i + 1) * 12 <= t.size(); ++i) {
        const std::uint8_t* r = t.data() + 6 + i * 12;
        const std::uint32_t platform = be16(r);
        const std::uint32_t language = be16(r + 4);
        const std::uint32_t id = be16(r + 6);
        const std::size_t length = be16(r + 8);
        const std::size_t offset = storage + be16(r + 10);
        if (offset + length > t.size() || length == 0) {
            continue;
        }
        const bool windows = platform == 3;
        const bool unicode = platform == 0;
        const bool mac = platform == 1 && be16(r + 2) == 0;
        if (!windows && !unicode && !mac) {
            continue;
        }
        int rank = windows ? (language == 0x0409 ? 4 : 2) : unicode ? 3 : 1;
        if (id == 16 || id == 17) {
            rank += 4;
        }
        if ((id == 1 || id == 16) && rank > family_rank) {
            family_rank = rank;
            face.family = name_string(t.data() + offset, length, !mac);
        } else if ((id == 2 || id == 17) && rank > style_rank) {
            style_rank = rank;
            face.style = name_string(t.data() + offset, length, !mac);
        }
    }
}

void add_span(std::vector<CodeSpan>& spans, char32_t first, char32_t last) {
    if (first > last || last > 0x10FFFF) {
        return;
    }
    spans.push_back({first, last});
}

void merge(std::vector<CodeSpan>& spans) {
    std::sort(spans.begin(), spans.end(), [](const CodeSpan& a, const CodeSpan& b) { return a.first < b.first; });
    std::vector<CodeSpan> out;
    for (const CodeSpan& s : spans) {
        if (!out.empty() && s.first <= out.back().last + 1) {
            out.back().last = std::max(out.back().last, s.last);
        } else {
            out.push_back(s);
        }
    }
    spans = std::move(out);
}

// Segment mapping of the Basic Multilingual Plane.
void read_format4(const std::uint8_t* p, std::size_t size, std::vector<CodeSpan>& spans) {
    if (size < 14) {
        return;
    }
    const std::size_t segments = be16(p + 6) / 2;
    if (14 + segments * 8 + 2 > size) {
        return;
    }
    const std::uint8_t* ends = p + 14;
    const std::uint8_t* starts = ends + segments * 2 + 2;
    for (std::size_t i = 0; i < segments; ++i) {
        const char32_t first = be16(starts + i * 2);
        const char32_t last = be16(ends + i * 2);
        if (first == 0xFFFF) {
            continue;  // the closing segment
        }
        add_span(spans, first, std::min<char32_t>(last, 0xFFFE));
    }
}

// Segmented coverage of all planes.
void read_format12(const std::uint8_t* p, std::size_t size, std::vector<CodeSpan>& spans) {
    if (size < 16) {
        return;
    }
    const std::size_t groups = be32(p + 12);
    for (std::size_t i = 0; i < groups && 16 + (i + 1) * 12 <= size; ++i) {
        const std::uint8_t* g = p + 16 + i * 12;
        add_span(spans, be32(g), be32(g + 4));
    }
}

void read_cmap(const std::vector<std::uint8_t>& t, FontFace& face) {
    if (t.size() < 4) {
        return;
    }
    const std::size_t count = be16(t.data() + 2);
    // The fullest Unicode subtable the font has: all planes over the Basic Multilingual Plane alone.
    std::size_t best = 0;
    int best_rank = 0;
    for (std::size_t i = 0; i < count && 4 + (i + 1) * 8 <= t.size(); ++i) {
        const std::uint8_t* r = t.data() + 4 + i * 8;
        const std::uint32_t platform = be16(r);
        const std::uint32_t encoding = be16(r + 2);
        const std::size_t offset = be32(r + 4);
        if (offset + 4 > t.size()) {
            continue;
        }
        const std::uint32_t format = be16(t.data() + offset);
        int rank = 0;
        if (format == 12 && ((platform == 3 && encoding == 10) || platform == 0)) {
            rank = 2;
        } else if (format == 4 && ((platform == 3 && encoding == 1) || platform == 0)) {
            rank = 1;
        }
        if (rank > best_rank) {
            best_rank = rank;
            best = offset;
        }
    }
    if (best_rank == 2) {
        read_format12(t.data() + best, t.size() - best, face.coverage);
    } else if (best_rank == 1) {
        read_format4(t.data() + best, t.size() - best, face.coverage);
    }
    merge(face.coverage);
}

std::optional<FontFace> read_face(const Reader& read, std::uint32_t at) {
    const std::optional<Directory> dir = read_directory(read, at);
    if (!dir || dir->cmap.length == 0) {
        return std::nullopt;
    }
    FontFace face;
    face.offset = at;
    read_cmap(read_table(read, dir->cmap), face);
    if (face.coverage.empty()) {
        return std::nullopt;
    }
    read_names(read_table(read, dir->name), face);
    const std::vector<std::uint8_t> os2 = read_table(read, dir->os2, 64);
    if (os2.size() >= 64) {
        const int weight = static_cast<int>(be16(os2.data() + 4));
        if (weight >= 1 && weight <= 1000) {
            face.weight = weight;
        }
        face.italic = (be16(os2.data() + 62) & 0x0201) != 0;  // italic or oblique
    }
    const std::vector<std::uint8_t> post = read_table(read, dir->post, 16);
    if (post.size() >= 16) {
        face.monospace = be32(post.data() + 12) != 0;
    }
    return face;
}

std::vector<FontFace> read_faces(const Reader& read, const std::filesystem::path& file) {
    std::vector<FontFace> out;
    const std::vector<std::uint8_t> head = read(0, 12);
    if (head.size() < 12) {
        return out;
    }
    std::vector<std::uint32_t> offsets;
    if (be32(head.data()) == kCollection) {
        const std::size_t count = std::min<std::size_t>(be32(head.data() + 8), kMaxFaces);
        const std::vector<std::uint8_t> table = read(12, count * 4);
        for (std::size_t i = 0; i * 4 + 4 <= table.size(); ++i) {
            offsets.push_back(be32(table.data() + i * 4));
        }
    } else {
        offsets.push_back(0);
    }
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        if (std::optional<FontFace> face = read_face(read, offsets[i])) {
            face->file = file;
            face->index = static_cast<int>(i);
            out.push_back(std::move(*face));
        }
    }
    return out;
}

}  // namespace

bool FontFace::has(char32_t cp) const {
    auto it = std::upper_bound(coverage.begin(), coverage.end(), cp,
                               [](char32_t value, const CodeSpan& s) { return value < s.first; });
    if (it == coverage.begin()) {
        return false;
    }
    --it;
    return cp <= it->last;
}

std::vector<FontFace> read_font_faces(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    const Reader read = [&in](std::uint32_t offset, std::size_t size) {
        std::vector<std::uint8_t> out(size);
        in.clear();
        in.seekg(static_cast<std::streamoff>(offset));
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        out.resize(static_cast<std::size_t>(std::max<std::streamsize>(in.gcount(), 0)));
        return out;
    };
    return read_faces(read, file);
}

std::vector<FontFace> parse_font_faces(std::span<const std::uint8_t> bytes, const std::filesystem::path& file) {
    const Reader read = [bytes](std::uint32_t offset, std::size_t size) {
        if (offset >= bytes.size()) {
            return std::vector<std::uint8_t>{};
        }
        const std::size_t n = std::min(size, bytes.size() - offset);
        return std::vector<std::uint8_t>(bytes.begin() + offset, bytes.begin() + offset + static_cast<std::ptrdiff_t>(n));
    };
    return read_faces(read, file);
}

}  // namespace sh::i18n
