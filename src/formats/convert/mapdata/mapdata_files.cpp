#include "formats/convert/mapdata/mapdata_files.h"

#include <atomic>
#include <fstream>
#include <random>
#include <iterator>
#include <system_error>

namespace sh::convert::mapdata {

namespace fs = std::filesystem;

namespace {

bool read_text(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

fs::path map_file_path(std::string_view map, Kind kind) {
    return fs::path("maps") / std::string(map) / (std::string(kind_name(kind)) + ".json");
}

Result<std::vector<std::uint8_t>> read_overlay(const fs::path& gamedata_dir, const MapLayout& layout) {
    const fs::path path = gamedata_dir / "raw" / overlay_path(layout.map);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Result<std::vector<std::uint8_t>>::fail("cannot read raw/" + overlay_path(layout.map));
    }
    return Result<std::vector<std::uint8_t>>::ok(
        std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

Result<void> write_file_atomic(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    // Unique per writer: several game instances may fill a missing file of a shared gamedata/ at once.
    static std::atomic<unsigned> counter{0};
    fs::path tmp = path;
    tmp += ".tmp-" + std::to_string(std::random_device{}()) + "-" + std::to_string(counter++);
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out.flush()) {
            out.close();
            fs::remove(tmp, ec);
            return Result<void>::fail("cannot write " + tmp.string());
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return Result<void>::fail("cannot rename " + tmp.string() + ": " + ec.message());
    }
    return Result<void>::ok();
}

void generate_map_files(const fs::path& gamedata_dir, const MapLayout& layout, bool overwrite, GenerateReport& report) {
    ++report.maps;
    const std::string map(layout.map);
    const auto overlay = read_overlay(gamedata_dir, layout);
    if (!overlay) {
        ++report.failed;
        report.warnings.push_back(map + ": " + overlay.error());
        return;
    }
    const auto decoded = decode_overlay(overlay.value(), layout);
    if (!decoded) {
        ++report.failed;
        report.warnings.push_back(map + ": " + decoded.error());
        return;
    }
    for (Kind kind : kAllKinds) {
        const fs::path relative = map_file_path(map, kind);
        const std::string where = relative.generic_string();
        const std::string text = write_json(decoded.value(), kind);

        // The fresh JSON has to give the disc bytes back; otherwise the file would change the game.
        MapData back;
        back.map = map;
        std::vector<std::string> warnings;
        const Result<void> parsed = read_json(text, kind, layout, back, warnings);
        const std::vector<std::string> diffs =
            parsed ? compare_kind(back, kind, layout, overlay.value()) : std::vector<std::string>{parsed.error()};
        if (!diffs.empty() || !warnings.empty()) {
            ++report.failed;
            for (const std::string& d : diffs) {
                report.warnings.push_back(where + ": round trip: " + d);
            }
            for (const std::string& w : warnings) {
                report.warnings.push_back(where + ": round trip: " + w);
            }
            continue;
        }
        ++report.round_trips;

        std::error_code ec;
        const fs::path path = gamedata_dir / relative;
        if (!overwrite && fs::exists(path, ec)) {
            ++report.kept;
        } else if (const Result<void> written = write_file_atomic(path, text); !written) {
            ++report.failed;
            report.warnings.push_back(where + ": " + written.error());
            continue;
        } else {
            ++report.written;
        }

        // The file the game will read (a kept one may have been edited) against the disc.
        std::string on_disk;
        MapData used;
        used.map = map;
        std::vector<std::string> file_warnings;
        if (!read_text(path, on_disk)) {
            ++report.failed;
            report.warnings.push_back(where + ": cannot read the file back");
            continue;
        }
        const Result<void> used_parsed = read_json(on_disk, kind, layout, used, file_warnings);
        for (const std::string& w : file_warnings) {
            report.warnings.push_back(where + ": " + w);
        }
        if (!used_parsed) {
            ++report.failed;
            report.warnings.push_back(where + ": " + used_parsed.error());
            continue;
        }
        ++report.files_checked;
        const std::vector<std::string> file_diffs = compare_kind(used, kind, layout, overlay.value());
        if (!file_diffs.empty()) {
            ++report.files_differ;
            for (const std::string& d : file_diffs) {
                report.warnings.push_back(where + " vs raw/" + overlay_path(map) + ": " + d);
            }
        }
    }

    // The unified scene view (map.json): the six kinds joined with typed entities. The handler
    // names come from the shipped gamedata/maps/<map>/handlers.json (repo data); without it the
    // callback entities still name their kind, just not their handler.
    std::vector<HandlerInfo> handlers;
    std::string handlers_text;
    const fs::path handlers_path = gamedata_dir / "maps" / map / "handlers.json";
    if (read_text(handlers_path, handlers_text)) {
        const Result<std::vector<HandlerInfo>> parsed = read_handlers_json(handlers_text);
        if (parsed) {
            handlers = std::move(parsed.value());
        } else {
            report.warnings.push_back("maps/" + map + "/handlers.json: " + parsed.error());
        }
    }
    const fs::path scene_path = gamedata_dir / "maps" / map / "map.json";
    const std::string scene_text = write_unified_json(decoded.value(), handlers);
    if (const Result<void> written = write_file_atomic(scene_path, scene_text); !written) {
        ++report.failed;
        report.warnings.push_back("maps/" + map + "/map.json: " + written.error());
    }
}

GenerateReport generate_map_files(const fs::path& gamedata_dir, bool overwrite) {
    GenerateReport report;
    for (const MapLayout& layout : map_layouts()) {
        generate_map_files(gamedata_dir, layout, overwrite, report);
    }
    return report;
}

std::string format_report_line(const GenerateReport& r) {
    std::string line = std::to_string(r.maps) + " maps, " + std::to_string(r.written) + " written, " +
                       std::to_string(r.kept) + " kept";
    if (r.failed) {
        line += ", " + std::to_string(r.failed) + " failed";
    }
    line += "; disc check: " + std::to_string(r.round_trips) + " round trips, " + std::to_string(r.files_checked) +
            " files, " + std::to_string(r.files_differ) + " differ";
    return line;
}

}  // namespace sh::convert::mapdata
