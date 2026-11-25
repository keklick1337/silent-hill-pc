// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
#include "render/rhi/pipeline_store.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <system_error>
#include <unordered_set>

namespace sh::rhi::pipeline_store {

namespace {

constexpr std::uint32_t kCacheMagic = 0x43504853u;     // "SHPC"
constexpr std::uint32_t kManifestMagic = 0x4D504853u;  // "SHPM"
constexpr std::uint32_t kFormat = 1;
constexpr std::size_t kMaxVersionBytes = 256;

class Writer {
public:
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void bytes(std::span<const std::uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); }
    std::vector<std::uint8_t> out;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> in) : in_(in) {}
    bool u32(std::uint32_t& v) {
        if (left() < 4) return fail();
        v = 0;
        for (int i = 0; i < 4; ++i) v |= std::uint32_t{in_[at_ + i]} << (8 * i);
        at_ += 4;
        return true;
    }
    bool u64(std::uint64_t& v) {
        if (left() < 8) return fail();
        v = 0;
        for (int i = 0; i < 8; ++i) v |= std::uint64_t{in_[at_ + i]} << (8 * i);
        at_ += 8;
        return true;
    }
    bool bytes(std::size_t n, std::span<const std::uint8_t>& out) {
        if (left() < n) return fail();
        out = in_.subspan(at_, n);
        at_ += n;
        return true;
    }
    std::size_t left() const { return in_.size() - at_; }
    bool ok() const { return ok_; }

private:
    bool fail() {
        ok_ = false;
        return false;
    }
    std::span<const std::uint8_t> in_;
    std::size_t at_ = 0;
    bool ok_ = true;
};

// Magic, format, payload size and hash, payload.
std::vector<std::uint8_t> seal(std::uint32_t magic, std::vector<std::uint8_t> header,
                               std::span<const std::uint8_t> payload) {
    Writer w;
    w.u32(magic);
    w.u32(kFormat);
    w.bytes(header);
    w.u64(payload.size());
    w.u64(hash64(payload));
    w.bytes(payload);
    return std::move(w.out);
}

// The payload when the rest of the file after `r`'s position is exactly size, hash and payload.
std::optional<std::span<const std::uint8_t>> payload(Reader& r) {
    std::uint64_t size = 0, hash = 0;
    if (!r.u64(size) || !r.u64(hash) || size != r.left()) return std::nullopt;
    std::span<const std::uint8_t> body;
    if (!r.bytes(static_cast<std::size_t>(size), body) || hash64(body) != hash) return std::nullopt;
    return body;
}

Writer identity_header(const Identity& id) {
    Writer h;
    h.u32(id.vendor_id);
    h.u32(id.device_id);
    h.u32(id.driver_version);
    h.bytes(id.cache_uuid);
    const std::size_t n = std::min(id.game_version.size(), kMaxVersionBytes);
    h.u32(static_cast<std::uint32_t>(n));
    h.bytes({reinterpret_cast<const std::uint8_t*>(id.game_version.data()), n});
    return h;
}

}  // namespace

std::uint64_t hash64(std::span<const std::uint8_t> bytes) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (std::uint8_t b : bytes) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

std::uint64_t hash64(std::span<const std::uint32_t> words) {
    return hash64(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(words.data()), words.size_bytes()));
}

std::vector<std::uint8_t> pack_cache(const Identity& identity, std::span<const std::uint8_t> driver_data) {
    return seal(kCacheMagic, identity_header(identity).out, driver_data);
}

std::optional<std::vector<std::uint8_t>> unpack_cache(std::span<const std::uint8_t> file, const Identity& identity) {
    if (file.size() > kMaxCacheBytes) return std::nullopt;
    const std::vector<std::uint8_t> expected = identity_header(identity).out;
    Reader r(file);
    std::uint32_t magic = 0, format = 0;
    std::span<const std::uint8_t> header;
    if (!r.u32(magic) || !r.u32(format) || magic != kCacheMagic || format != kFormat) return std::nullopt;
    if (!r.bytes(expected.size(), header) || !std::equal(header.begin(), header.end(), expected.begin()))
        return std::nullopt;
    const auto body = payload(r);
    if (!body) return std::nullopt;
    return std::vector<std::uint8_t>(body->begin(), body->end());
}

std::vector<std::uint8_t> pack_manifest(const Manifest& manifest) {
    Writer w;
    w.u32(static_cast<std::uint32_t>(manifest.shaders.size()));
    for (const auto& [hash, words] : manifest.shaders) {
        w.u64(hash);
        w.u32(static_cast<std::uint32_t>(words.size()));
        w.bytes({reinterpret_cast<const std::uint8_t*>(words.data()), words.size() * sizeof(std::uint32_t)});
    }
    w.u32(static_cast<std::uint32_t>(manifest.recipes.size()));
    for (const Recipe& recipe : manifest.recipes) {
        w.u32(recipe.idle_starts);
        w.u32(static_cast<std::uint32_t>(recipe.shaders.size()));
        for (std::uint64_t s : recipe.shaders) w.u64(s);
        w.u32(static_cast<std::uint32_t>(recipe.bytes.size()));
        w.bytes(recipe.bytes);
    }
    return seal(kManifestMagic, {}, w.out);
}

std::optional<Manifest> unpack_manifest(std::span<const std::uint8_t> file) {
    if (file.size() > kMaxManifestBytes) return std::nullopt;
    Reader outer(file);
    std::uint32_t magic = 0, format = 0;
    if (!outer.u32(magic) || !outer.u32(format) || magic != kManifestMagic || format != kFormat) return std::nullopt;
    const auto body = payload(outer);
    if (!body) return std::nullopt;
    Reader r(*body);
    Manifest m;
    std::uint32_t shaders = 0;
    if (!r.u32(shaders)) return std::nullopt;
    for (std::uint32_t i = 0; i < shaders; ++i) {
        std::uint64_t hash = 0;
        std::uint32_t count = 0;
        std::span<const std::uint8_t> code;
        if (!r.u64(hash) || !r.u32(count) || !r.bytes(std::size_t{count} * 4, code)) return std::nullopt;
        std::vector<std::uint32_t> words(count);
        std::memcpy(words.data(), code.data(), code.size());
        if (hash64(words) != hash) return std::nullopt;
        m.shaders.emplace(hash, std::move(words));
    }
    std::uint32_t recipes = 0;
    if (!r.u32(recipes) || recipes > kMaxRecipes) return std::nullopt;
    m.recipes.resize(recipes);
    for (Recipe& recipe : m.recipes) {
        std::uint32_t count = 0;
        if (!r.u32(recipe.idle_starts) || !r.u32(count) || count > 16) return std::nullopt;
        recipe.shaders.resize(count);
        for (std::uint64_t& s : recipe.shaders) {
            if (!r.u64(s) || !m.shaders.contains(s)) return std::nullopt;
        }
        std::span<const std::uint8_t> bytes;
        if (!r.u32(count) || !r.bytes(count, bytes)) return std::nullopt;
        recipe.bytes.assign(bytes.begin(), bytes.end());
    }
    if (!r.ok() || r.left() != 0) return std::nullopt;
    return m;
}

Manifest merge(const Manifest& earlier, const Manifest& created) {
    Manifest out;
    std::unordered_set<std::uint64_t> keys;
    for (const Recipe& recipe : created.recipes) {
        if (!keys.insert(recipe.key()).second) continue;
        out.recipes.push_back(recipe);
        out.recipes.back().idle_starts = 0;
    }
    std::vector<Recipe> idle;
    for (const Recipe& recipe : earlier.recipes) {
        if (recipe.idle_starts + 1 >= kIdleStartsKept || keys.contains(recipe.key())) continue;
        keys.insert(recipe.key());
        idle.push_back(recipe);
        idle.back().idle_starts = recipe.idle_starts + 1;
    }
    std::stable_sort(idle.begin(), idle.end(),
                     [](const Recipe& a, const Recipe& b) { return a.idle_starts < b.idle_starts; });
    for (Recipe& recipe : idle) out.recipes.push_back(std::move(recipe));
    if (out.recipes.size() > kMaxRecipes) out.recipes.resize(kMaxRecipes);
    std::size_t bytes = 0;
    std::size_t kept = 0;
    for (const Recipe& recipe : out.recipes) {
        std::size_t need = recipe.bytes.size() + 64;
        for (std::uint64_t s : recipe.shaders) {
            if (out.shaders.contains(s)) continue;
            const auto* words = created.shaders.contains(s)   ? &created.shaders.at(s)
                                : earlier.shaders.contains(s) ? &earlier.shaders.at(s)
                                                              : nullptr;
            if (!words) {
                need = SIZE_MAX;
                break;
            }
            need += words->size() * 4 + 16;
        }
        // A recipe whose shaders are unknown, or that would push the file past its bound, ends the list.
        if (need == SIZE_MAX || bytes + need > kMaxManifestBytes - 1024) break;
        for (std::uint64_t s : recipe.shaders) {
            if (out.shaders.contains(s)) continue;
            out.shaders.emplace(s, created.shaders.contains(s) ? created.shaders.at(s) : earlier.shaders.at(s));
        }
        bytes += need;
        ++kept;
    }
    out.recipes.resize(kept);
    return out;
}

std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& file, std::size_t max_bytes) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    if (ec || size > max_bytes) return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return std::nullopt;
    return bytes;
}

bool write_file_atomic(const std::filesystem::path& file, std::span<const std::uint8_t> bytes) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

}  // namespace sh::rhi::pipeline_store
