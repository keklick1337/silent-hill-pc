#pragma once

// The game's file table on the real image (data/Silent Hill (v1.1).bin), for tests that run over every file of a
// kind. Skips the test when the image is not there. Needs sh_disc, sh_iso9660 and sh_archive.

#include "framework/sh_test.h"
#include "formats/archive/sh_archive.h"
#include "formats/disc/disc_image.h"
#include "formats/iso9660/iso9660.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef SH_PROJECT_ROOT
#define SH_PROJECT_ROOT "../.."
#endif

namespace sh::test {

struct GameDisc {
    std::shared_ptr<const sh::disc::Image> image;
    std::vector<sh::archive::Entry> table;
    std::optional<sh::archive::Archive> silent;

    std::optional<std::vector<std::uint8_t>> read(std::string_view name) const {
        for (const auto& e : table) {
            if (e.name == name) {
                return silent->read(e);
            }
        }
        return std::nullopt;
    }

    // A reader for converters' sibling files.
    auto reader() const {
        return [this](std::string_view p) { return read(p); };
    }
};

inline std::optional<GameDisc> open_game_disc() {
    namespace fs = std::filesystem;
    const fs::path path = fs::path(SH_PROJECT_ROOT) / "data" / "Silent Hill (v1.1).bin";
    if (!fs::exists(path)) {
        SH_SKIP("data/Silent Hill (v1.1).bin is not present");
    }
    GameDisc disc;
    disc.image = std::shared_ptr<const sh::disc::Image>(sh::disc::Image::open(path));
    if (!disc.image) {
        return std::nullopt;
    }
    const auto volume = sh::iso9660::Volume::open(disc.image);
    if (!volume) {
        return std::nullopt;
    }
    const auto slus = volume->find("SLUS_007.07");
    const auto silent = volume->find("SILENT.");
    const auto hill = volume->find("HILL.");
    if (!slus || !silent || !hill) {
        return std::nullopt;
    }
    const std::shared_ptr<const sh::ByteSource> exe(volume->open_file(*slus));
    const std::shared_ptr<const sh::ByteSource> silent_src(volume->open_file(*silent));
    const auto table = sh::archive::read_file_table(*exe, {silent->lba, (silent->size + 2047) / 2048},
                                                    {hill->lba, (hill->size + 2047) / 2048});
    if (!table) {
        return std::nullopt;
    }
    disc.table = *table;
    disc.silent = sh::archive::Archive::open(silent_src, sh::archive::Region::Silent);
    if (!disc.silent) {
        return std::nullopt;
    }
    return disc;
}

// Runs `converter` over every file of the image it accepts and checks encode(decode(x)) == x when it is lossless.
// Returns the number of files converted; prints failures.
template <typename Conv>
std::size_t round_trip_all(const GameDisc& disc, const Conv& converter, std::string_view extension,
                           std::size_t* seen = nullptr) {
    std::size_t converted = 0;
    for (const auto& e : disc.table) {
        if (e.name.size() < extension.size() ||
            e.name.compare(e.name.size() - extension.size(), extension.size(), extension) != 0) {
            continue;
        }
        if (seen) {
            ++*seen;
        }
        const auto raw = disc.read(e.name);
        if (!raw || !converter.accepts(*raw, e.name)) {
            continue;
        }
        const auto out = converter.decode(*raw, e.name);
        SH_CHECK(out);
        if (!out) {
            std::printf("  %s: %s\n", e.name.c_str(), out.error().c_str());
            continue;
        }
        if (converter.lossless()) {
            const auto back = converter.encode(out.value());
            SH_CHECK(back);
            if (!back || back.value() != *raw) {
                std::printf("  %s: does not round-trip%s\n", e.name.c_str(), back ? "" : (": " + back.error()).c_str());
                SH_CHECK(false);
                continue;
            }
        }
        ++converted;
    }
    return converted;
}

}  // namespace sh::test
