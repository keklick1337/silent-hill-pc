#include "engine/tables/table_store.h"

#include "engine/mods/data_merge.h"
#include "engine/mods/mods.h"
#include "formats/convert/gamefiles/game_files.h"

#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

namespace sh::tables {

namespace fs = std::filesystem;

namespace {

// The disc's files are there: raw/ of an older import, or the store that replaces it.
bool has_game_files(const fs::path& gamedata_dir) {
    std::error_code ec;
    return sh::convert::gamefiles::has_raw(gamedata_dir) ||
           fs::exists(gamedata_dir / sh::convert::gamefiles::kManifest, ec);
}

std::string trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(" \t\r");
    return std::string(s.substr(first, last - first + 1));
}

Result<void> write_file_atomic(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        return Result<void>::fail("cannot create " + path.parent_path().string() + ": " + ec.message());
    }
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            out.close();
            fs::remove(tmp, ec);
            return Result<void>::fail("cannot write " + tmp.string());
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return Result<void>::fail("cannot rename " + tmp.string() + " to " + path.filename().string() + ": " +
                                  ec.message());
    }
    return Result<void>::ok();
}

std::string list_of(const std::vector<std::size_t>& indices) {
    std::string list;
    for (std::size_t i = 0; i < indices.size() && i < 16; ++i) {
        list += (i ? ", " : "") + std::to_string(indices[i]);
    }
    if (indices.size() > 16) {
        list += ", ...";
    }
    return list;
}

}  // namespace

void apply_config_text(StoreConfig& config, std::string_view text, std::vector<std::string>& warnings) {
    std::size_t line_no = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
        ++line_no;
        if (const auto hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        const auto eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));
        for (char& c : value) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (key == "import_tables") {
            warnings.push_back("line " + std::to_string(line_no) + ": import_tables is gone (the tables always "
                               "come from gamedata/tables/ and the disc); the value is ignored");
        } else if (key == "import_overwrite") {
            if (value == "0" || value == "1") {
                config.overwrite = value == "1";
            } else {
                warnings.push_back("line " + std::to_string(line_no) + ": import_overwrite = " + value +
                                   " is not 0 or 1; 0 is used");
            }
        }
    }
}

fs::path anim_table_path(const ShAnimTableInfo& info, std::string_view overlay) {
    std::string name = info.name;
    if (!overlay.empty()) {
        name += "@" + std::string(overlay);
    }
    return fs::path("tables") / "anim" / (name + ".json");
}

const AnimTable& DefaultAnimTables::for_overlay(std::string_view overlay) const {
    for (const auto& [name, table] : variants) {
        if (name == overlay) {
            return table;
        }
    }
    return base;
}

DefaultAnimTables default_anim_tables(const fs::path& gamedata_dir, const ShAnimTableInfo& info,
                                      std::span<const ShAnimFunc> funcs) {
    DefaultAnimTables out;
    out.base.symbol = info.symbol;
    if (gamedata_dir.empty() || !has_game_files(gamedata_dir)) {
        out.disc_failed = true;
        out.notes.push_back(std::string(info.name) +
                            ": neither raw/ nor convert/files/ is present: the disc import is required");
        return out;
    }
    std::vector<std::pair<std::string, AnimTable>> copies;
    for (std::size_t s = 0; s < info.sourceCount; ++s) {
        const ShAnimSource& source = info.sources[s];
        const std::optional<std::vector<std::uint8_t>> bytes =
            sh::convert::gamefiles::read_game_file(gamedata_dir, source.file);
        if (!bytes) {
            out.disc_failed = true;
            out.notes.push_back(std::string(info.name) + ": the game file " + source.file + " cannot be read");
            return out;
        }
        auto table = anim_table_from_overlay(info, *bytes, source.va, funcs);
        if (!table) {
            out.disc_failed = true;
            out.notes.push_back(std::string(info.name) + ": " + source.file + ": " + table.error());
            return out;
        }
        if (s == 0) {
            out.base = std::move(table.value());
        } else {
            copies.emplace_back(overlay_of(source), std::move(table.value()));
        }
    }
    for (auto& [overlay, table] : copies) {
        if (!diff_anim_tables(table, out.base).empty()) {
            out.variants.emplace_back(overlay, std::move(table));
        }
    }
    return out;
}

Result<WriteReport> write_default_anim_tables(const fs::path& gamedata_dir, const ShAnimTableInfo& info,
                                              std::span<const ShAnimFunc> funcs, bool overwrite) {
    WriteReport report;
    const DefaultAnimTables defaults = default_anim_tables(gamedata_dir, info, funcs);
    if (defaults.disc_failed) {
        // A table that cannot be read from the disc is a failure the whole import reports, not a file
        // written from somewhere else: nothing is compiled in.
        const std::string why =
            defaults.notes.empty() ? std::string("the disc could not be read") : defaults.notes.front();
        return Result<WriteReport>::fail(why);
    }
    report.notes = defaults.notes;
    std::vector<std::pair<std::string, const AnimTable*>> files{{std::string(), &defaults.base}};
    for (const auto& [overlay, table] : defaults.variants) {
        files.emplace_back(overlay, &table);
    }
    std::error_code ec;
    for (const auto& [overlay, table] : files) {
        const fs::path relative = anim_table_path(info, overlay);
        if (!overwrite && fs::exists(gamedata_dir / relative, ec)) {
            report.kept.push_back(relative.generic_string());
            continue;
        }
        const Result<void> written = write_file_atomic(gamedata_dir / relative, write_anim_table(*table, info, overlay));
        if (!written) {
            return Result<WriteReport>::fail(written.error());
        }
        report.written.push_back(relative.generic_string());
    }
    return Result<WriteReport>::ok(std::move(report));
}

TableStore::TableStore(StoreConfig config) : config_(std::move(config)) {}

void TableStore::note(const std::string& message) const {
    if (config_.log) {
        config_.log(SH_TABLES_LOG_INFO, "[TABLES] " + message);
    }
}

void TableStore::warn(const std::string& message) const {
    if (config_.log) {
        config_.log(SH_TABLES_LOG_WARN, "[TABLES] " + message);
    }
}

void TableStore::error(const std::string& message) const {
    if (config_.log) {
        config_.log(SH_TABLES_LOG_ERROR, "[TABLES] " + message);
    }
}

void TableStore::discover_mods() {
    if (mods_discovered_) {
        return;
    }
    mods_discovered_ = true;
    std::error_code ec;
    if (config_.mods_dir.empty() || !fs::is_directory(config_.mods_dir, ec)) {
        return;
    }
    std::vector<std::string> warnings;
    const std::vector<mods::ModInfo> found = mods::discover(config_.mods_dir, warnings);
    for (const std::string& w : warnings) {
        warn("mods: " + w);
    }
    // discover() sorts by ascending priority; the highest priority wins, so it is looked at first.
    for (auto it = found.rbegin(); it != found.rend(); ++it) {
        mods_.push_back({it->id, it->gameplay, it->root});
    }
}

const DefaultAnimTables& TableStore::defaults_for(const ShAnimTableInfo& info) {
    auto it = defaults_.find(info.name);
    if (it == defaults_.end()) {
        it = defaults_.emplace(info.name, default_anim_tables(config_.gamedata_dir, info, config_.funcs)).first;
        for (const std::string& n : it->second.notes) {
            it->second.disc_failed ? error(n) : warn(n);
        }
    }
    return it->second;
}

void TableStore::ensure_files(const ShAnimTableInfo& info) {
    if (files_checked_[info.name]) {
        return;
    }
    files_checked_[info.name] = true;
    // Only inside a gamedata/ that has the game files: an empty gamedata/ would stop the first-run import (it
    // does not replace a non-empty directory), and without them there are no disc values to write.
    if (!has_game_files(config_.gamedata_dir)) {
        return;
    }
    auto written = write_default_anim_tables(config_.gamedata_dir, info, config_.funcs, config_.overwrite);
    if (!written) {
        warn(std::string(info.name) + ": " + written.error());
        return;
    }
    for (const std::string& path : written.value().written) {
        note((config_.gamedata_dir / path).string() +
             (config_.overwrite ? ": rewritten from the disc (import_overwrite = 1)" : ": created from the disc"));
    }
}

TableStore::AnimLoad TableStore::load_anim(const ShAnimTableInfo& info, std::string_view overlay) {
    const std::string key = std::string(info.name) + "@" + std::string(overlay);
    auto it = loaded_.find(key);
    if (it == loaded_.end()) {
        it = loaded_.emplace(key, resolve(info, overlay)).first;
    }
    return it->second;
}

TableStore::AnimLoad TableStore::resolve(const ShAnimTableInfo& info, std::string_view overlay) {
    if (!announced_) {
        announced_ = true;
        note("tables from " + (config_.gamedata_dir / "tables").string());
    }
    const DefaultAnimTables& defaults = defaults_for(info);
    const AnimTable& expected = defaults.for_overlay(overlay);
    AnimLoad result{to_game_entries(expected, info, config_.funcs), false};
    discover_mods();
    ensure_files(info);

    // A variant file of this overlay first, then the table itself; for each, gamedata/'s copy with every mod's
    // patch merged over it in load order (entries by "index").
    std::vector<fs::path> relatives;
    if (!overlay.empty()) {
        relatives.push_back(anim_table_path(info, overlay));
    }
    relatives.push_back(anim_table_path(info));
    std::error_code ec;
    fs::path chosen;
    fs::path relative;
    const ModDir* mod = nullptr;
    std::optional<std::string> merged_text;
    for (const fs::path& rel : relatives) {
        std::vector<mods::DataLayer> layers;
        for (auto it = mods_.rbegin(); it != mods_.rend(); ++it) {  // mods_ is highest first
            if (fs::is_regular_file(it->root / rel, ec)) {
                layers.push_back({it->id, it->root / rel, it->root, it->gameplay});
                mod = &*it;
            }
        }
        std::vector<std::string> merge_warnings;
        Result<mods::MergedFile> merged =
            mods::merge_layers(rel.generic_string(), config_.gamedata_dir / rel, layers, merge_warnings);
        for (const std::string& w : merge_warnings) {
            warn(w);
        }
        if (!merged) {
            mod = nullptr;
            if (!merged.error().starts_with("no file")) {
                error(merged.error());
            }
            continue;
        }
        mods::emit_merge_trace(merged.value());
        chosen = merged.value().path;
        if (merged.value().mods.empty()) {
            mod = nullptr;
        }
        merged_text = std::move(merged.value().text);
        relative = rel;
        break;
    }
    if (chosen.empty()) {
        // Nothing is compiled in: without the imported file the table can only come
        // from the game files, and without them the array stays zero. Both mean the import did not run.
        const std::string missing = relative.empty() ? anim_table_path(info, overlay).generic_string()
                                                     : relative.generic_string();
        if (defaults.disc_failed) {
            error(missing + " is missing and so are the game files (raw/, convert/files/): run the disc import");
        } else {
            error(missing + " is missing: run the disc import; the disc values are used");
        }
        return result;
    }
    const bool from_disc = !defaults.disc_failed;
    const std::string fallback = from_disc ? "; the disc values are used" : "; the table stays zero";
    auto table = read_anim_table(*merged_text, info, config_.funcs);
    if (!table) {
        error(chosen.string() + ": " + table.error() + fallback);
        return result;
    }

    const std::vector<std::size_t> changed = diff_anim_tables(table.value(), expected);
    const std::string reference = from_disc ? "the disc" : "the broken-table zero fill";
    const std::string where = overlay.empty() ? std::string() : " in " + std::string(overlay);
    if (mod) {
        gameplay_changed_ = true;
        note("gameplay mod '" + mod->id + "' replaces " + relative.generic_string() + where + " (" +
             std::to_string(changed.size()) + " entries differ from " + reference + ")");
        if (!mod->gameplay) {
            warn("mod '" + mod->id + "' changes a gameplay table but its mod.json does not say \"gameplay\": true");
        }
    } else if (!changed.empty()) {
        gameplay_changed_ = true;
        note(chosen.string() + where + ": " + std::to_string(changed.size()) + " entries differ from " + reference +
             " (" + list_of(changed) + "): gameplay change");
    } else if (!overlay.empty() && relative != anim_table_path(info)) {
        note(relative.generic_string() + ": the copy of " + std::string(overlay) + " is used there");
    }
    for (const std::string& n : lint_anim_table(table.value(), expected)) {
        warn(chosen.string() + ": " + n);
    }
    result.entries = to_game_entries(table.value(), info, config_.funcs);
    result.from_file = true;
    return result;
}

}  // namespace sh::tables
