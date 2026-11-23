#include "formats/convert/level/map_unified_files.h"

#include "formats/common/byte_source.h"
#include "formats/convert/level/map_areas.h"
#include "formats/convert/level/map_unified.h"
#include "formats/convert/mapdata/mapdata_files.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>

namespace sh::convert::level {
namespace {

namespace fs = std::filesystem;
namespace md = sh::convert::mapdata;

std::optional<std::vector<std::uint8_t>> read_bytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Indexed {
    std::string path;
    std::uint64_t size = 0;
};

// The BG/ rows of index.tsv (path, size, ...).
Result<std::vector<Indexed>> packed_rows(const fs::path& gamedata_dir) {
    std::ifstream in(gamedata_dir / "index.tsv", std::ios::binary);
    if (!in) {
        return Result<std::vector<Indexed>>::fail("cannot read index.tsv");
    }
    std::vector<Indexed> rows;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        const std::size_t t1 = line.find('\t');
        const std::size_t t2 = t1 == std::string::npos ? std::string::npos : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) {
            continue;
        }
        Indexed row;
        row.path = line.substr(0, t1);
        if (unified_group_of(row.path).empty()) {
            continue;
        }
        try {
            row.size = std::stoull(line.substr(t1 + 1, t2 - t1 - 1));
        } catch (...) {
            return Result<std::vector<Indexed>>::fail("index.tsv: bad size for " + row.path);
        }
        rows.push_back(std::move(row));
    }
    return Result<std::vector<Indexed>>::ok(std::move(rows));
}

struct Group {
    std::string name;
    std::vector<Indexed> cells;
    std::optional<Indexed> global;
    std::vector<Indexed> libraries;
};

std::vector<Group> make_groups(const std::vector<Indexed>& rows) {
    std::map<std::string, Group> groups;
    for (const Indexed& row : rows) {
        const std::string name = unified_group_of(row.path);
        Group& g = groups[name];
        g.name = name;
        if (row.path.size() > 4 && row.path.compare(row.path.size() - 4, 4, ".IPD") == 0) {
            g.cells.push_back(row);
        } else if (global_plm_of_area(name) == row.path) {
            g.global = row;
        } else {
            g.libraries.push_back(row);
        }
    }
    std::vector<Group> out;
    for (auto& [name, g] : groups) {
        std::sort(g.cells.begin(), g.cells.end(), [](const Indexed& a, const Indexed& b) { return a.path < b.path; });
        std::sort(g.libraries.begin(), g.libraries.end(),
                  [](const Indexed& a, const Indexed& b) { return a.path < b.path; });
        out.push_back(std::move(g));
    }
    return out;
}

struct GroupResult {
    bool written = false;
    bool kept = false;
    bool failed = false;
    unsigned files = 0;
    unsigned round_trips = 0;
    std::uint64_t glb_bytes = 0;
    std::vector<std::string> warnings;
};

GroupResult build_group(const fs::path& gamedata_dir, const Group& group, bool overwrite,
                        const TextureCatalog& catalog) {
    GroupResult r;
    const fs::path raw = gamedata_dir / "raw";
    const std::string relative = unified_relative_path(group.name);
    const fs::path target = gamedata_dir / relative;

    std::vector<SourceFile> sources;  // in the order cells, global, libraries
    auto load = [&](const Indexed& row) -> std::optional<SourceFile> {
        auto bytes = read_bytes(raw / row.path);
        if (!bytes || bytes->size() < row.size) {
            r.warnings.push_back(relative + ": cannot read raw/" + row.path);
            r.failed = true;
            return std::nullopt;
        }
        return SourceFile{row.path, row.size, std::move(*bytes)};
    };

    std::error_code ec;
    const bool exists = fs::is_regular_file(target, ec);
    UnifiedInput input;
    input.group = group.name;
    input.catalog = &catalog;
    for (const Indexed& row : group.cells) {
        if (auto f = load(row)) {
            input.cells.push_back(std::move(*f));
        }
    }
    if (group.global) {
        input.global = load(*group.global);
    }
    for (const Indexed& row : group.libraries) {
        if (auto f = load(row)) {
            input.libraries.push_back(std::move(*f));
        }
    }
    if (r.failed) {
        return r;
    }

    if (!overwrite && exists) {
        r.kept = true;
    } else {
        for (const MapArea& m : map_areas()) {
            if (m.area != group.name) {
                continue;
            }
            const md::MapLayout* layout = md::find_layout(m.map);
            if (!layout) {
                continue;
            }
            auto overlay = md::read_overlay(gamedata_dir, *layout);
            auto decoded = overlay ? md::decode_overlay(overlay.value(), *layout) : Result<md::MapData>::fail(overlay.error());
            if (!decoded) {
                r.warnings.push_back(relative + ": map " + std::string(m.map) + " left out: " + decoded.error());
                continue;
            }
            input.maps.push_back({std::string(m.map), std::move(decoded.value())});
        }
        UnifiedStats stats;
        auto glb = write_unified(input, &stats);
        for (const std::string& w : stats.warnings) {
            r.warnings.push_back(relative + ": " + w);
        }
        if (!glb) {
            r.warnings.push_back(relative + ": " + glb.error());
            r.failed = true;
            return r;
        }
        const std::string text(glb.value().begin(), glb.value().end());
        if (auto w = md::write_file_atomic(target, text); !w) {
            r.warnings.push_back(relative + ": " + w.error());
            r.failed = true;
            return r;
        }
        r.written = true;
    }

    // The file the game will read (a kept one may be stale) against raw/.
    std::vector<const SourceFile*> all;
    for (const SourceFile& f : input.cells) {
        all.push_back(&f);
    }
    if (input.global) {
        all.push_back(&*input.global);
    }
    for (const SourceFile& f : input.libraries) {
        all.push_back(&f);
    }
    r.files = static_cast<unsigned>(all.size());
    const std::unique_ptr<ByteSource> source = open_file_source(target);
    if (!source) {
        r.warnings.push_back(relative + ": cannot open the file back");
        r.failed = true;
        return r;
    }
    r.glb_bytes = source->size();
    auto index = read_unified_index(*source);
    if (!index) {
        r.warnings.push_back(relative + ": " + index.error());
        r.failed = true;
        return r;
    }
    for (const SourceFile* f : all) {
        const auto it = std::find_if(index.value().files.begin(), index.value().files.end(),
                                     [&](const UnifiedEntry& e) { return e.game_path == f->game_path; });
        if (it == index.value().files.end()) {
            r.warnings.push_back(relative + ": " + f->game_path + " is not in the file");
            r.failed = true;
            continue;
        }
        auto back = read_unified_entry(*source, *it);
        if (!back || it->size != f->size || back.value() != f->stored) {
            r.warnings.push_back(relative + ": " + f->game_path + " differs from raw/" +
                                 (back ? "" : " (" + back.error() + ")"));
            r.failed = true;
            continue;
        }
        ++r.round_trips;
    }
    if (index.value().files.size() != all.size()) {
        r.warnings.push_back(relative + ": holds " + std::to_string(index.value().files.size()) + " files, raw/ has " +
                             std::to_string(all.size()));
        r.failed = true;
    }
    return r;
}

}  // namespace

UnifiedReport generate_unified_maps(const fs::path& gamedata_dir, bool overwrite, unsigned threads) {
    UnifiedReport report;
    auto rows = packed_rows(gamedata_dir);
    if (!rows) {
        report.warnings.push_back(rows.error());
        ++report.failed;
        return report;
    }
    const std::vector<Group> groups = make_groups(rows.value());
    const fs::path raw = gamedata_dir / "raw";
    const TextureCatalog catalog([raw](std::string_view game_path) { return read_bytes(raw / std::string(game_path)); });

    std::vector<GroupResult> results(groups.size());
    std::atomic<std::size_t> next{0};
    auto worker = [&] {
        for (std::size_t i = next++; i < groups.size(); i = next++) {
            results[i] = build_group(gamedata_dir, groups[i], overwrite, catalog);
        }
    };
    if (threads == 0) {
        threads = std::max(1u, std::thread::hardware_concurrency());
    }
    threads = std::min<unsigned>(threads, static_cast<unsigned>(std::max<std::size_t>(1, groups.size())));
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < threads; ++t) {
        pool.emplace_back(worker);
    }
    worker();
    for (std::thread& t : pool) {
        t.join();
    }

    for (const GroupResult& r : results) {
        ++report.groups;
        report.written += r.written ? 1 : 0;
        report.kept += r.kept ? 1 : 0;
        report.failed += r.failed ? 1 : 0;
        report.files += r.files;
        report.round_trips += r.round_trips;
        report.glb_bytes += r.glb_bytes;
        report.warnings.insert(report.warnings.end(), r.warnings.begin(), r.warnings.end());
    }
    return report;
}

std::string format_unified_line(const UnifiedReport& r) {
    char size[32];
    std::snprintf(size, sizeof size, "%.1f MiB", static_cast<double>(r.glb_bytes) / (1024.0 * 1024.0));
    std::string line = std::to_string(r.groups) + " areas, " + std::to_string(r.written) + " written, " +
                       std::to_string(r.kept) + " kept";
    if (r.failed) {
        line += ", " + std::to_string(r.failed) + " failed";
    }
    line += "; " + std::to_string(r.files) + " files packed, " + std::to_string(r.round_trips) + " round trips; " + size;
    return line;
}

}  // namespace sh::convert::level
