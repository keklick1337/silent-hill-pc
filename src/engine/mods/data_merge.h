// A JSON data file as base + every active mod's patch of it, in load order (json_merge.h has the rules). This is
// what the loaders of gamedata/*.json read instead of "the file of the last mod", so several mods can change the
// same file. A mod replaces the file whole only with a top-level "$replace": true AND the path in its manifest's
// "replace" list.
#pragma once

#include "engine/mods/json_merge.h"
#include "engine/mods/mods.h"
#include "formats/common/status.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::mods {

// One mod's copy of the file.
struct DataLayer {
    std::string mod_id;
    std::filesystem::path file;
    std::filesystem::path root;  // the mod folder (its mod.json)
    bool gameplay = false;
};

struct MergedFile {
    // The text the loader parses. With only one file in play it is that file's bytes unchanged, so a run
    // without mods reads exactly what it read before; a merge gives the merged document as compact JSON.
    std::string text;
    // The one file read when nothing was merged; else the last mod's file (for messages).
    std::filesystem::path path;
    bool base = false;                // the base file took part
    std::vector<DataLayer> mods;      // the mods whose file took part, in load order
    std::vector<std::string> trace;   // "mod X: <file>: modified 2, added 1, removed 0", one per mod

    bool merged() const { return mods.size() + (base ? 1 : 0) > 1; }
    bool from_mods() const { return !mods.empty(); }
    // The last mod that took part, "" for none.
    std::string last_mod() const { return mods.empty() ? std::string() : mods.back().mod_id; }
    // For messages: the file, or "<last file> (merged: base + mods a, b)".
    std::string source() const;
};

// The merge rules of a gamedata path ("maps/map2_s00/world_objects.json" -> objects by "name", ...): the table in
// data_merge.cpp, documented in docs/modding/data.md.
const MergeRules& rules_for(std::string_view relative);

// True when the mod at `root` lists `relative` in its mod.json "replace" (case-insensitive, '/' separators).
// The one place the whole-file replacement asks the manifest.
bool replace_listed(const std::filesystem::path& root, std::string_view relative);

// The copies of `relative` in `mods` (load order, lowest first).
std::vector<DataLayer> layers_of(const std::vector<ModInfo>& mods, std::string_view relative);

// Base (`base_file`, may be absent: then the first mod's file is the base) + `layers` in order. Fails only when
// no file exists or none can be read; a file that does not parse is skipped with a warning (a broken base with
// mods: the first mod's copy stands in for it). Without layers the base comes back unparsed, as it is.
// Merge warnings and the trace lines name the mod and the file.
Result<MergedFile> merge_layers(std::string_view relative, const std::filesystem::path& base_file,
                                const std::vector<DataLayer>& layers, std::vector<std::string>& warnings);

// gamedata_dir/relative + the copies in the active mods of `mods_dir` (discover()). The trace goes to the merge
// trace sink.
Result<MergedFile> read_data_file(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                  const std::filesystem::path& relative, std::vector<std::string>& warnings);

// read_data_file for the loaders that only want the text: warnings go to the trace sink ("warning: ..."),
// `source` (optional) gets MergedFile::source(); a missing file fails with "cannot read <gamedata file>".
Result<std::string> read_data_text(const std::filesystem::path& gamedata_dir, const std::filesystem::path& mods_dir,
                                   const std::filesystem::path& relative, std::string* source = nullptr);

// The same for the game's registry (ShVfs_ReadDataJson): base_dir/relative + the registry's active mods.
Result<MergedFile> read_registry_data_file(std::string_view relative, const std::filesystem::path& base_dir,
                                           std::vector<std::string>& warnings);

void emit_merge_trace(const MergedFile& file);
void emit_merge_warnings(const std::vector<std::string>& warnings);

}  // namespace sh::mods
