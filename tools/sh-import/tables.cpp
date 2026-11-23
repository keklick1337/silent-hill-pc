#include "sh-import/tables.h"

#include "engine/tables/anim_table.h"
#include "engine/tables/table_store.h"
#include "engine/tables/anim_table_registry.h"

#include <fstream>
#include <iterator>
#include <span>
#include <system_error>

namespace sh::import_tool {

namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> read_bytes(const fs::path& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    ok = static_cast<bool>(in);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string read_text(const fs::path& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    ok = static_cast<bool>(in);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

}  // namespace

TablesReport generate_tables(const fs::path& gamedata_dir, bool overwrite) {
    TablesReport report;
    const std::span<const ShAnimFunc> funcs(g_AnimTableFuncs, g_AnimTableFuncCount);
    for (unsigned t = 0; t < g_AnimTableInfoCount; ++t) {
        const ShAnimTableInfo& info = *g_AnimTableInfos[t];
        const auto written = sh::tables::write_default_anim_tables(gamedata_dir, info, funcs, overwrite);
        if (!written) {
            ++report.failed;
            report.warnings.push_back(sh::tables::anim_table_path(info).generic_string() + ": " + written.error());
            continue;
        }
        report.written += static_cast<unsigned>(written.value().written.size());
        report.kept += static_cast<unsigned>(written.value().kept.size());
        report.warnings.insert(report.warnings.end(), written.value().notes.begin(), written.value().notes.end());

        // Each copy on the disc against the file the game uses while that overlay is loaded.
        for (unsigned s = 0; s < info.sourceCount; ++s) {
            const ShAnimSource& source = info.sources[s];
            const std::string overlay = sh::tables::overlay_of(source);
            std::error_code ec;
            fs::path relative = sh::tables::anim_table_path(info, overlay);
            if (!fs::exists(gamedata_dir / relative, ec)) {
                relative = sh::tables::anim_table_path(info);
            }
            const std::string where = relative.generic_string() + " vs raw/" + source.file;
            bool ok = false;
            const std::string text = read_text(gamedata_dir / relative, ok);
            auto table = ok ? sh::tables::read_anim_table(text, info, funcs)
                            : Result<sh::tables::AnimTable>::fail("cannot read the file back");
            if (!table) {
                ++report.failed;
                report.warnings.push_back(where + ": " + table.error());
                continue;
            }
            const std::vector<ShAnimEntry> entries = sh::tables::to_game_entries(table.value(), info, funcs);
            // Only the entries the file holds are table data; FLOATSTINGER's last two are neighbouring rodata.
            const std::span<const ShAnimEntry> compared(entries.data(), info.fileCount);
            const std::vector<std::uint8_t> bytes = read_bytes(gamedata_dir / "raw" / source.file, ok);
            if (!ok) {
                report.warnings.push_back(where + ": cannot read the overlay");
                continue;
            }
            const auto diffs = sh::tables::compare_with_overlay(compared, bytes, source.va);
            if (!diffs) {
                report.warnings.push_back(where + ": " + diffs.error());
                continue;
            }
            ++report.copies_checked;
            if (!diffs.value().empty()) {
                ++report.copies_differ;
                report.entries_differ += static_cast<unsigned>(diffs.value().size());
                for (const std::string& d : diffs.value()) {
                    report.warnings.push_back(where + ": " + d);
                }
            }
        }
    }
    return report;
}

std::string format_tables_line(const TablesReport& report) {
    std::string line = std::to_string(report.written) + " written, " + std::to_string(report.kept) + " kept";
    if (report.failed) {
        line += ", " + std::to_string(report.failed) + " failed";
    }
    line += "; raw/ check: " + std::to_string(report.copies_checked) + " copies, " +
            std::to_string(report.copies_differ) + " differ in " + std::to_string(report.entries_differ) + " entries";
    return line;
}

}  // namespace sh::import_tool
