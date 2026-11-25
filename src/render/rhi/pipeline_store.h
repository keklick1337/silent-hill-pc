// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The files of the pipeline cache on disk (RENDER.md section 2.2d), independent of the API: the driver's cache data
// behind a header that names the game build and the driver it was made by, and the manifest of the pipelines the
// game created, which the next start builds before play. A file that does not match or is damaged reads as absent.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sh::rhi::pipeline_store {

inline constexpr std::size_t kMaxCacheBytes = std::size_t{64} << 20;
inline constexpr std::size_t kMaxManifestBytes = std::size_t{32} << 20;
inline constexpr std::size_t kMaxRecipes = 4096;
// A pipeline no start created for this many starts in a row leaves the manifest (a shader changed, a mod went).
inline constexpr std::uint32_t kIdleStartsKept = 16;

// FNV-1a, 64 bits.
std::uint64_t hash64(std::span<const std::uint8_t> bytes);
std::uint64_t hash64(std::span<const std::uint32_t> words);

// What the driver's cache data is valid for. Any difference, the game's build included, discards the data.
struct Identity {
    std::string game_version;  // sh::version::full()
    std::uint32_t vendor_id = 0;
    std::uint32_t device_id = 0;
    std::uint32_t driver_version = 0;
    std::array<std::uint8_t, 16> cache_uuid{};  // VkPhysicalDeviceProperties::pipelineCacheUUID
    bool operator==(const Identity&) const = default;
};

std::vector<std::uint8_t> pack_cache(const Identity& identity, std::span<const std::uint8_t> driver_data);
// The driver's data; nothing for another build, device or driver, a truncated or damaged file, or one too large.
std::optional<std::vector<std::uint8_t>> unpack_cache(std::span<const std::uint8_t> file, const Identity& identity);

// One pipeline as the backend describes it (opaque here), with the shaders it reads.
struct Recipe {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint64_t> shaders;  // hash64 of each shader's code, in the manifest's table
    std::uint32_t idle_starts = 0;       // starts since one last created it
    std::uint64_t key() const { return hash64(bytes); }
};

struct Manifest {
    std::map<std::uint64_t, std::vector<std::uint32_t>> shaders;  // SPIR-V by hash64
    std::vector<Recipe> recipes;
};

std::vector<std::uint8_t> pack_manifest(const Manifest& manifest);
// Nothing for a damaged or truncated file, or a recipe that names a shader the table lacks.
std::optional<Manifest> unpack_manifest(std::span<const std::uint8_t> file);

// The manifest the next start reads: what this run created (idle 0), then the earlier manifest's other recipes one
// start idler, without those idle for kIdleStartsKept starts, at most kMaxRecipes (the least idle kept), and only
// the shaders the kept recipes read.
Manifest merge(const Manifest& earlier, const Manifest& created);

// The whole file; nothing when it is missing, unreadable or larger than `max_bytes`.
std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& file, std::size_t max_bytes);
// Writes beside the file and renames it into place, so a crash or a full disk leaves the old file or none.
bool write_file_atomic(const std::filesystem::path& file, std::span<const std::uint8_t> bytes);

}  // namespace sh::rhi::pipeline_store
