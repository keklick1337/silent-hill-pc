// SPDX-License-Identifier: GPL-3.0-or-later
// The sector reader behind sh::disc (disc.h): the installed source resolves an LBA through the VFS; with no source,
// reads go to the raw 2352-byte-sector image that open_image named.
#include "engine/platform/disc.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

namespace sh::disc {

namespace {

constexpr long kRawSectorBytes = 2352;

struct Image {
    std::mutex lock;  // reads come from the audio, FMV and loader threads
    std::string path;
    std::FILE* file = nullptr;
    int sectors = 0;
};

Image g_image;
std::atomic<SectorSource*> g_source{nullptr};

// Caller holds the lock.
bool image_open() {
    if (g_image.file)
        return true;
    if (g_image.path.empty())
        return false;

    g_image.file = std::fopen(g_image.path.c_str(), "rb");
    if (!g_image.file) {
        std::fprintf(stderr, "CD image '%s' not found.\n", g_image.path.c_str());
        return false;
    }
    std::fseek(g_image.file, 0, SEEK_END);
    g_image.sectors = static_cast<int>(std::ftell(g_image.file) / kRawSectorBytes);
    return true;
}

}  // namespace

void set_source(SectorSource* source) { g_source.store(source); }

bool has_source() { return g_source.load() != nullptr; }

void open_image(std::string_view path) {
    std::lock_guard<std::mutex> guard(g_image.lock);
    g_image.path = path;
}

bool available() {
    if (has_source())
        return true;
    std::lock_guard<std::mutex> guard(g_image.lock);
    return g_image.file != nullptr || !g_image.path.empty();
}

int sector_count() {
    if (SectorSource* source = g_source.load())
        return source->sector_count();
    std::lock_guard<std::mutex> guard(g_image.lock);
    return image_open() ? g_image.sectors : 0;
}

bool read_sector(int lba, SectorKind kind, void* dst) {
    if (SectorSource* source = g_source.load())
        return source->read_sector(lba, kind, dst);

    const auto bytes = static_cast<std::size_t>(sector_bytes(kind));
    const long skip = kind == SectorKind::Xa ? 16 : 24;  // sync, header (and subheader for data) before the payload

    std::lock_guard<std::mutex> guard(g_image.lock);
    if (!image_open() || lba < 0 || lba >= g_image.sectors ||
        std::fseek(g_image.file, static_cast<long>(lba) * kRawSectorBytes + skip, SEEK_SET) != 0 ||
        std::fread(dst, bytes, 1, g_image.file) != 1) {
        std::memset(dst, 0, bytes);
        return false;
    }
    return true;
}

DrivePacing& drive_pacing() {
    static DrivePacing pacing;
    return pacing;
}

}  // namespace sh::disc
