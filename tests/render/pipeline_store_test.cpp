// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The pipeline cache files (render/rhi/pipeline_store.h): the driver's data comes back only for the build and driver
// that wrote it, a damaged or cut file reads as absent, and the manifest keeps what the runs created.

#include "framework/sh_test.h"
#include "render/rhi/pipeline_store.h"

#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace ps = sh::rhi::pipeline_store;
namespace fs = std::filesystem;

namespace {

ps::Identity identity() {
    ps::Identity id;
    id.game_version = "1.0.0+3000.abcdef0";
    id.vendor_id = 0x13B5;
    id.device_id = 0x92020010;
    id.driver_version = 0x8000000;
    for (std::size_t i = 0; i < id.cache_uuid.size(); ++i) id.cache_uuid[i] = static_cast<std::uint8_t>(i * 7);
    return id;
}

ps::Recipe recipe(std::uint8_t tag, std::uint64_t shader, std::uint32_t idle = 0) {
    ps::Recipe r;
    r.bytes = {tag, 1, 2, 3};
    r.shaders = {shader};
    r.idle_starts = idle;
    return r;
}

}  // namespace

SH_TEST(pipeline_cache_comes_back_for_the_same_build_and_driver) {
    const std::vector<std::uint8_t> data{9, 8, 7, 6, 5, 4, 3, 2, 1};
    const std::vector<std::uint8_t> file = ps::pack_cache(identity(), data);
    const auto back = ps::unpack_cache(file, identity());
    SH_CHECK(back.has_value() && *back == data);
}

SH_TEST(pipeline_cache_of_another_build_or_driver_is_discarded) {
    const std::vector<std::uint8_t> file = ps::pack_cache(identity(), std::vector<std::uint8_t>{1, 2, 3});
    ps::Identity other = identity();
    other.game_version = "1.0.0+3001.abcdef1";
    SH_CHECK(!ps::unpack_cache(file, other));
    other = identity();
    other.driver_version += 1;
    SH_CHECK(!ps::unpack_cache(file, other));
    other = identity();
    other.device_id += 1;
    SH_CHECK(!ps::unpack_cache(file, other));
    other = identity();
    other.vendor_id = 0x10DE;
    SH_CHECK(!ps::unpack_cache(file, other));
    other = identity();
    other.cache_uuid[15] ^= 1;
    SH_CHECK(!ps::unpack_cache(file, other));
}

SH_TEST(pipeline_cache_damaged_or_cut_reads_as_absent) {
    const std::vector<std::uint8_t> file = ps::pack_cache(identity(), std::vector<std::uint8_t>(100, 0x5A));
    for (std::size_t cut = 0; cut < file.size(); cut += 7) {
        const std::vector<std::uint8_t> part(file.begin(), file.begin() + static_cast<std::ptrdiff_t>(cut));
        SH_CHECK(!ps::unpack_cache(part, identity()));
    }
    std::vector<std::uint8_t> flipped = file;
    flipped.back() ^= 0x01;
    SH_CHECK(!ps::unpack_cache(flipped, identity()));
    std::vector<std::uint8_t> longer = file;
    longer.push_back(0);
    SH_CHECK(!ps::unpack_cache(longer, identity()));
    SH_CHECK(!ps::unpack_cache(std::vector<std::uint8_t>(ps::kMaxCacheBytes + 1), identity()));
}

SH_TEST(pipeline_manifest_round_trips_and_rejects_damage) {
    ps::Manifest m;
    m.shaders[ps::hash64(std::vector<std::uint32_t>{0x07230203u, 1, 2})] = {0x07230203u, 1, 2};
    const std::uint64_t s = m.shaders.begin()->first;
    m.recipes = {recipe(1, s, 3), recipe(2, s)};
    const std::vector<std::uint8_t> file = ps::pack_manifest(m);
    const auto back = ps::unpack_manifest(file);
    SH_CHECK(back.has_value());
    SH_CHECK(back->shaders == m.shaders);
    SH_CHECK(back->recipes.size() == 2 && back->recipes[0].bytes == m.recipes[0].bytes &&
             back->recipes[0].idle_starts == 3 && back->recipes[1].shaders == m.recipes[1].shaders);
    std::vector<std::uint8_t> flipped = file;
    flipped[flipped.size() / 2] ^= 0x10;
    SH_CHECK(!ps::unpack_manifest(flipped));
    SH_CHECK(!ps::unpack_manifest(std::vector<std::uint8_t>(file.begin(), file.end() - 1)));
    // A recipe that names a shader the table lacks.
    ps::Manifest orphan = m;
    orphan.recipes.push_back(recipe(3, s + 1));
    SH_CHECK(!ps::unpack_manifest(ps::pack_manifest(orphan)));
}

SH_TEST(pipeline_manifest_merge_keeps_what_runs_create) {
    const std::vector<std::uint32_t> code_a{0x07230203u, 10}, code_b{0x07230203u, 20};
    const std::uint64_t a = ps::hash64(code_a), b = ps::hash64(code_b);
    ps::Manifest earlier;
    earlier.shaders = {{a, code_a}, {b, code_b}};
    earlier.recipes = {recipe(1, a, 0), recipe(2, b, ps::kIdleStartsKept - 1), recipe(3, a, 4)};
    ps::Manifest created;
    created.shaders = {{a, code_a}};
    created.recipes = {recipe(3, a, 0), recipe(4, a, 0)};
    const ps::Manifest next = ps::merge(earlier, created);
    // This run's first, idle 0; the earlier ones one start idler; the one idle too long and its shader gone.
    SH_CHECK(next.recipes.size() == 3);
    SH_CHECK(next.recipes[0].bytes[0] == 3 && next.recipes[0].idle_starts == 0);
    SH_CHECK(next.recipes[1].bytes[0] == 4 && next.recipes[1].idle_starts == 0);
    SH_CHECK(next.recipes[2].bytes[0] == 1 && next.recipes[2].idle_starts == 1);
    SH_CHECK(next.shaders.size() == 1 && next.shaders.contains(a));
}

SH_TEST(pipeline_store_writes_atomically) {
    static const unsigned tag = std::random_device{}();
    const fs::path dir = fs::temp_directory_path() / ("sh_pipeline_store_" + std::to_string(tag));
    const fs::path file = dir / "sub" / "vulkan.cache";
    const std::vector<std::uint8_t> first{1, 2, 3}, second{4, 5};
    SH_CHECK(ps::write_file_atomic(file, first));
    SH_CHECK(ps::read_file(file, 16) == first);
    SH_CHECK(ps::write_file_atomic(file, second));
    SH_CHECK(ps::read_file(file, 16) == second);
    SH_CHECK(!fs::exists(dir / "sub" / "vulkan.cache.tmp"));
    SH_CHECK(!ps::read_file(file, 1));  // larger than allowed
    SH_CHECK(!ps::read_file(dir / "missing", 16));
    std::error_code ec;
    fs::remove_all(dir, ec);
}
