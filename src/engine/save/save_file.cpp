// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "engine/save/save_file.h"

#include "formats/common/crc32.h"

#include <cstdio>
#include <cstring>
#include <system_error>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sh::save {

namespace {

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

std::uint16_t get_u16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

std::uint32_t get_u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::string tag_text(const Tag& tag) { return std::string(tag.data(), tag.size()); }

}  // namespace

const Chunk* SaveFile::find(Tag tag) const {
    for (const Chunk& chunk : chunks) {
        if (chunk.tag == tag) {
            return &chunk;
        }
    }
    return nullptr;
}

void SaveFile::put(Tag tag, std::vector<std::uint8_t> data) {
    for (Chunk& chunk : chunks) {
        if (chunk.tag == tag) {
            chunk.data = std::move(data);
            return;
        }
    }
    chunks.push_back(Chunk{tag, std::move(data)});
}

std::vector<std::uint8_t> encode(const SaveFile& file) {
    std::vector<std::uint8_t> payload;
    for (const Chunk& chunk : file.chunks) {
        payload.insert(payload.end(), chunk.tag.begin(), chunk.tag.end());
        put_u32(payload, static_cast<std::uint32_t>(chunk.data.size()));
        payload.insert(payload.end(), chunk.data.begin(), chunk.data.end());
        while (payload.size() % 4 != 0) {
            payload.push_back(0);
        }
    }

    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + payload.size());
    out.insert(out.end(), kMagic.begin(), kMagic.end());
    put_u16(out, file.version);
    put_u16(out, static_cast<std::uint16_t>(file.kind));
    put_u32(out, static_cast<std::uint32_t>(payload.size()));
    put_u32(out, crc32(payload.data(), payload.size()));
    put_u32(out, static_cast<std::uint32_t>(file.chunks.size()));
    out.resize(kHeaderSize, 0);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

Result<SaveFile> decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderSize || std::memcmp(bytes.data(), kMagic.data(), kMagic.size()) != 0) {
        return Result<SaveFile>::fail("not a save file (no SHPCSAVE header)");
    }
    const std::uint8_t* h = bytes.data();
    const std::uint16_t version = get_u16(h + 8);
    if (version == 0 || version > kFormatVersion) {
        return Result<SaveFile>::fail("save format version " + std::to_string(version) + " is newer than this build (" +
                                      std::to_string(kFormatVersion) + ")");
    }
    const std::uint32_t payload_size = get_u32(h + 12);
    if (payload_size != bytes.size() - kHeaderSize) {
        return Result<SaveFile>::fail("size mismatch: header says " + std::to_string(payload_size) + " bytes of data, file has " +
                                      std::to_string(bytes.size() - kHeaderSize));
    }
    const std::uint8_t* payload = h + kHeaderSize;
    if (crc32(payload, payload_size) != get_u32(h + 16)) {
        return Result<SaveFile>::fail("CRC mismatch: the file is damaged");
    }
    const std::uint32_t chunk_count = get_u32(h + 20);

    SaveFile file;
    file.version = version;
    file.kind = static_cast<FileKind>(get_u16(h + 10));
    std::size_t pos = 0;
    for (std::uint32_t i = 0; i < chunk_count; ++i) {
        if (payload_size - pos < 8) {
            return Result<SaveFile>::fail("chunk " + std::to_string(i) + " is cut off");
        }
        Chunk chunk;
        std::memcpy(chunk.tag.data(), payload + pos, 4);
        const std::uint32_t size = get_u32(payload + pos + 4);
        pos += 8;
        if (size > payload_size - pos) {
            return Result<SaveFile>::fail("chunk " + tag_text(chunk.tag) + " runs past the end of the file");
        }
        chunk.data.assign(payload + pos, payload + pos + size);
        pos += size;
        pos = (pos + 3) & ~static_cast<std::size_t>(3);
        if (pos > payload_size) {
            pos = payload_size;
        }
        file.chunks.push_back(std::move(chunk));
    }
    return Result<SaveFile>::ok(std::move(file));
}

std::vector<std::uint8_t> encode_summary(const SlotSummary& s) {
    std::vector<std::uint8_t> out;
    put_u32(out, static_cast<std::uint32_t>(s.sequence));
    put_u32(out, s.play_time);
    put_u16(out, s.save_count);
    out.push_back(s.location);
    out.push_back(s.next_fear ? 1 : 0);
    out.push_back(s.extra_hours);
    out.push_back(s.special_items);
    put_u16(out, 0);
    return out;
}

Result<SlotSummary> decode_summary(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 16) {
        return Result<SlotSummary>::fail("INFO chunk is " + std::to_string(bytes.size()) + " bytes, expected 16");
    }
    SlotSummary s;
    s.sequence = static_cast<std::int32_t>(get_u32(bytes.data()));
    s.play_time = get_u32(bytes.data() + 4);
    s.save_count = get_u16(bytes.data() + 8);
    s.location = bytes[10];
    s.next_fear = bytes[11] != 0;
    s.extra_hours = bytes[12];
    s.special_items = bytes[13];
    return Result<SlotSummary>::ok(s);
}

Result<void> write_file_atomic(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return Result<void>::fail("cannot create " + path.parent_path().string() + ": " + ec.message());
        }
    }
    std::filesystem::path temp = path;
    temp += ".tmp";

    std::FILE* f = nullptr;
#if defined(_WIN32)
    f = _wfopen(temp.wstring().c_str(), L"wb");
#else
    f = std::fopen(temp.string().c_str(), "wb");
#endif
    if (f == nullptr) {
        return Result<void>::fail("cannot write " + temp.string());
    }
    const bool written = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    bool flushed = std::fflush(f) == 0;
#if defined(_WIN32)
    flushed = flushed && _commit(_fileno(f)) == 0;
#else
    flushed = flushed && fsync(fileno(f)) == 0;
#endif
    const bool closed = std::fclose(f) == 0;
    if (!written || !flushed || !closed) {
        std::filesystem::remove(temp, ec);
        return Result<void>::fail("cannot write " + temp.string() + " (disk full?)");
    }

#if defined(_WIN32)
    // std::filesystem::rename does not replace an existing file on every Windows runtime.
    if (!MoveFileExW(temp.wstring().c_str(), path.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp, ec);
        return Result<void>::fail("cannot replace " + path.string());
    }
#else
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return Result<void>::fail("cannot replace " + path.string() + ": " + ec.message());
    }
    // The rename is durable only once the directory entry is on disk.
    const int dir = ::open(path.parent_path().empty() ? "." : path.parent_path().string().c_str(), O_RDONLY);
    if (dir >= 0) {
        fsync(dir);
        ::close(dir);
    }
#endif
    return Result<void>::ok();
}

Result<std::vector<std::uint8_t>> read_whole_file(const std::filesystem::path& path) {
    std::FILE* f = nullptr;
#if defined(_WIN32)
    f = _wfopen(path.wstring().c_str(), L"rb");
#else
    f = std::fopen(path.string().c_str(), "rb");
#endif
    if (f == nullptr) {
        return Result<std::vector<std::uint8_t>>::fail("cannot open " + path.string());
    }
    std::vector<std::uint8_t> out;
    std::uint8_t buf[16384];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.insert(out.end(), buf, buf + n);
    }
    const bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (failed) {
        return Result<std::vector<std::uint8_t>>::fail("cannot read " + path.string());
    }
    return Result<std::vector<std::uint8_t>>::ok(std::move(out));
}

}  // namespace sh::save
