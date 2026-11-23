#include "engine/vfs/disc_sectors.h"

#include "engine/manifest/map_manifest.h"
#include "formats/archive/sh_archive.h"
#include "formats/common/byte_source.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace sh::vfs {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kCachedFiles = 8;

// Data sectors inside HILL. keep their user data 8 bytes into the 2336-byte sector, after the subheader.
constexpr std::uint64_t kXaDataOffset = 8;

struct HillRange {
    std::uint32_t first = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t end = 0;
};

// "HILL:lba=39359+2152" -> 39359, 2152.
bool parse_hill_source(std::string_view source, std::uint32_t& lba, std::uint32_t& sectors) {
    constexpr std::string_view prefix = "HILL:lba=";
    if (!source.starts_with(prefix)) {
        return false;
    }
    source.remove_prefix(prefix.size());
    const std::size_t plus = source.find('+');
    if (plus == std::string_view::npos) {
        return false;
    }
    const auto a = std::from_chars(source.data(), source.data() + plus, lba);
    const auto b = std::from_chars(source.data() + plus + 1, source.data() + source.size(), sectors);
    return a.ec == std::errc() && a.ptr == source.data() + plus && b.ec == std::errc() &&
           b.ptr == source.data() + source.size();
}

// CdSearchFile names look like "\\MOVIE\\INTRO.STR;1": the ISO version suffix is not part of the game path.
std::string game_path(std::string_view name) {
    std::string path = normalize(name);
    const std::size_t semi = path.rfind(';');
    if (semi != std::string::npos && path.find('/', semi) == std::string::npos) {
        path.resize(semi);
    }
    return path;
}

}  // namespace

std::unique_ptr<DiscSectors> DiscSectors::open(const Vfs& vfs, const fs::path& gamedata_dir, std::string& error) {
    const std::vector<GameDataFile> rows = read_gamedata_index(gamedata_dir, error);
    if (rows.empty()) {
        return nullptr;
    }

    std::unordered_map<std::string, const GameDataFile*> by_path;
    HillRange hill;
    for (const GameDataFile& row : rows) {
        by_path.emplace(row.path, &row);
        std::uint32_t lba = 0;
        std::uint32_t sectors = 0;
        if (parse_hill_source(row.source, lba, sectors)) {
            hill.first = std::min(hill.first, lba);
            hill.end = std::max(hill.end, lba + sectors);
        }
    }
    if (hill.end == 0) {
        error = "index.tsv has no HILL. rows";
        return nullptr;
    }

    // The LBA map is the game's own file table, from the imported executable rather than whatever a mod puts in
    // its place. The executable name comes from the game manifest (disc.exe_file) with this disc's default as
    // fallback. SILENT. ends where HILL. starts on this disc; the table's start check needs no tighter bound.
    const char* manifest_exe = MapManifest_DiscString("exe_file");
    const std::string exe_name = manifest_exe != nullptr ? manifest_exe : "SLUS_007.07";
    const std::unique_ptr<ByteSource> exe = open_file_source(gamedata_dir / "raw" / exe_name);
    if (!exe) {
        error = "no raw/" + exe_name;
        return nullptr;
    }
    const std::optional<std::vector<archive::Entry>> table =
        archive::read_file_table(*exe, archive::Layout{0, hill.first}, archive::Layout{hill.first, hill.end - hill.first});
    if (!table) {
        error = "raw/" + exe_name + " holds no file table";
        return nullptr;
    }

    std::vector<SectorRun> runs;
    runs.reserve(table->size());
    for (const archive::Entry& entry : *table) {
        const std::string path = normalize(entry.name);
        const auto it = by_path.find(path);
        if (it == by_path.end() || it->second->size != entry.size) {
            error = "index.tsv does not match the file table at " + path;
            return nullptr;
        }
        SectorRun run;
        run.lba = entry.lba;
        run.sectors = entry.sectors;
        run.path = path;
        run.size = entry.size;
        run.hill = entry.region == archive::Region::Hill;
        if (run.sectors != 0) {
            runs.push_back(std::move(run));
        }
    }
    std::sort(runs.begin(), runs.end(), [](const SectorRun& a, const SectorRun& b) { return a.lba < b.lba; });
    for (std::size_t i = 1; i < runs.size(); ++i) {
        if (runs[i - 1].lba + runs[i - 1].sectors > runs[i].lba) {
            error = "file table runs overlap at " + runs[i].path;
            return nullptr;
        }
    }
    return std::unique_ptr<DiscSectors>(new DiscSectors(vfs, std::move(runs)));
}

DiscSectors::DiscSectors(const Vfs& vfs, std::vector<SectorRun> runs) : vfs_(vfs), runs_(std::move(runs)) {}

std::uint32_t DiscSectors::sector_count() const {
    return runs_.empty() ? 0 : runs_.back().lba + runs_.back().sectors;
}

const SectorRun* DiscSectors::run_at(std::uint32_t lba) const {
    const auto it = std::upper_bound(runs_.begin(), runs_.end(), lba,
                                     [](std::uint32_t value, const SectorRun& run) { return value < run.lba; });
    if (it == runs_.begin()) {
        return nullptr;
    }
    const SectorRun& run = *std::prev(it);
    return lba - run.lba < run.sectors ? &run : nullptr;
}

const SectorRun* DiscSectors::find(std::string_view name) const {
    const std::string path = game_path(name);
    for (const SectorRun& run : runs_) {
        if (run.path == path) {
            return &run;
        }
    }
    return nullptr;
}

std::shared_ptr<const ByteSource> DiscSectors::source_for(const SectorRun& run) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < cache_.size(); ++i) {
        if (cache_[i].run == &run) {
            Cached hit = std::move(cache_[i]);
            cache_.erase(cache_.begin() + static_cast<std::ptrdiff_t>(i));
            cache_.push_back(hit);
            return hit.source;
        }
    }
    // The layer is looked up on every open, so the file of the highest layer that has the path is the one read.
    const Layer* layer = vfs_.find(run.path);
    std::shared_ptr<const ByteSource> source = layer ? layer->open_stored(run.path) : nullptr;
    if (!source) {
        return nullptr;
    }
    if (cache_.size() == kCachedFiles) {
        cache_.erase(cache_.begin());
    }
    cache_.push_back(Cached{&run, source});
    return source;
}

bool DiscSectors::read(std::uint32_t lba, SectorKind kind, std::uint8_t* dst) const {
    const std::size_t bytes = sector_bytes(kind);
    std::memset(dst, 0, bytes);

    const SectorRun* run = run_at(lba);
    if (!run || (!run->hill && kind == SectorKind::Xa)) {
        return false;
    }
    const std::shared_ptr<const ByteSource> source = source_for(*run);
    if (!source) {
        return false;
    }
    const std::uint64_t index = lba - run->lba;
    const std::uint64_t offset = run->hill ? index * kXaSectorBytes + (kind == SectorKind::Data ? kXaDataOffset : 0)
                                           : index * kDataSectorBytes;
    // A mod file may be shorter than the sectors of the original; what it lacks reads as zeros.
    const std::uint64_t size = source->size();
    const std::size_t count = offset >= size ? 0 : static_cast<std::size_t>(std::min<std::uint64_t>(bytes, size - offset));
    return count == 0 || source->read_at(offset, dst, count);
}

bool read_image_sector(const disc::Image& image, std::uint32_t lba, SectorKind kind, std::uint8_t* dst) {
    std::array<std::uint8_t, 2352> raw{};
    if (!image.read_raw(lba, raw.data())) {
        std::memset(dst, 0, sector_bytes(kind));
        return false;
    }
    std::memcpy(dst, raw.data() + (kind == SectorKind::Xa ? 16 : 24), sector_bytes(kind));
    return true;
}

}  // namespace sh::vfs
