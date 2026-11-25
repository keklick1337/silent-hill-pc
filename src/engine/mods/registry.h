// The mods of this run: one process-wide Registry scans the mods folders once, resolves
// the load order (load_order.h) and answers "which file does this game path come from" for every loader. Its
// edits (switching a mod on or off, moving it) are saved to mods/load_order.json and apply on the next start
// (mods with "requires_restart": false at once, apply_live).
#pragma once

#include "engine/mods/load_order.h"
#include "formats/common/status.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sh::mods {

// The mod API version a manifest's "api" range is checked against; the engine version is kEngineVersion
// (load_order.h), the VERSION file's.
inline constexpr int kApiMajor = 1;
inline constexpr int kApiMinor = 0;

inline constexpr std::string_view kUserStateFile = "load_order.json";

// mods/load_order.json: {"format": 1, "mods": [{"id": "a", "enabled": true}, ...], "notices": ["..."]}.
Result<UserState> parse_user_state(std::string_view text);
std::string write_user_state(const UserState& state);

// Reads every <dir>/<folder>/mod.json (or mod.cfg) in `dirs`, folders by name, dirs in the given order.
// Folders starting with '.' are skipped; folders without a manifest are reported in `warnings`.
std::vector<Candidate> scan_mod_dirs(const std::vector<std::filesystem::path>& dirs, std::vector<std::string>& warnings);

struct RegistryConfig {
    std::vector<std::filesystem::path> dirs;  // <exe>/mods first, then mods_dir
    std::filesystem::path state_file;          // "" = <dirs[0]>/load_order.json
    std::filesystem::path base_dir;            // gamedata/: tells a replaced file from an added one
    Environment env{kEngineVersion, Version{kApiMajor, kApiMinor, 0, {}}, {}};
    // Old mod locations looked at once for the migration notice (gamedata/load, gamedata/texturemods, ...).
    std::vector<std::filesystem::path> legacy_dirs;
};

// One file of a mod, relative to the mod folder ("convert/textures/CHARA/HERO.png").
struct ModFile {
    std::string path;
    bool replaces = false;           // the base game data has the same path (else the mod adds it)
    std::vector<std::string> also_in;  // other active mods with the same path, in load order
    bool wins = true;                // no later active mod has it
};

class Registry {
public:
    // Scans now. A second call replaces everything (tests); the game configures once at start.
    void configure(RegistryConfig config);
    bool configured() const;
    const RegistryConfig& config() const { return config_; }
    // True when `mods_dir` is one of the configured folders (same folder after canonicalisation).
    bool covers(const std::filesystem::path& mods_dir) const;

    // Rereads folders, manifests and the state file; the running set becomes the pending one. Only safe for
    // assets the engine reads again on demand (textures, text): the caller decides what to reload.
    void rescan();
    // Rebuilds the file index of the running set only: files added to or removed from active mods become
    // visible, while the load order, the states and the pending edits stay as they are (`mods reload`).
    void reindex();
    // Reads the folders and manifests again for the pending choice only: a mod folder made while the game runs
    // (the editor's "New mod") can then be switched on and saved. The running set and its files stay as they are.
    void rescan_pending();
    // Size and modification time of every indexed file, by "<mod id>/<path>": `mods reload` compares two of them to
    // say which files changed.
    using FileStamps = std::map<std::string, std::uint64_t>;
    FileStamps file_stamps() const;

    // The running set: resolved at start.
    const Plan& plan() const { return plan_; }
    std::vector<const ResolvedMod*> active() const { return plan_.active(); }

    // The highest active mod that has `relative` (a gamedata path with '/'; case-insensitive), or nothing. For
    // "<name>.wav" a mod's "<name>.opus" counts too (the sound readers take either, by content).
    std::optional<std::filesystem::path> find(std::string_view relative) const;
    // Every active mod that has it, lowest first (for data that mods patch one over another).
    std::vector<std::filesystem::path> find_all(std::string_view relative) const;
    // The id of the mod `find` would answer with, "" for none.
    std::string owner_of(std::string_view relative) const;

    // The files of an active mod with what they do; empty for an unknown or inactive id.
    std::vector<ModFile> files(std::string_view id) const;
    // Startup / console summary: one line per mod, then problems and notices.
    std::vector<std::string> report(bool with_files) const;
    // One-time notices about old mod folders that are no longer read; marks them shown in the state file.
    std::vector<std::string> take_new_notices();
    const std::vector<std::string>& notices() const { return notices_; }

    // The player's pending choice, as the Mods screen shows it (resolved again after every edit).
    const Plan& pending() const { return pending_plan_; }
    const UserState& pending_user_state() const { return pending_user_; }
    bool set_enabled(std::string_view id, bool enabled);
    // Moves a mod `delta` places in the pending list (all mods, the inactive ones after the active ones).
    bool move(std::string_view id, int delta);
    Result<void> save();
    // The pending choice loads another set or order of mods that need a restart ("requires_restart", the default).
    bool restart_pending() const;
    // The pending choice differs from the running one only in mods that apply without a restart.
    bool live_pending() const;
    // When live_pending(): makes the saved pending choice the running set (rescan); their files are read again on
    // demand. True when it did. Call after save().
    bool apply_live();

private:
    void resolve_all();
    void build_index();
    std::filesystem::path state_path() const;

    RegistryConfig config_;
    bool configured_ = false;
    std::vector<Candidate> candidates_;
    std::vector<std::string> scan_warnings_;
    UserState user_;
    UserState pending_user_;
    Plan plan_;
    Plan pending_plan_;
    std::vector<std::string> notices_;
    std::vector<std::string> notice_keys_;

    // Lower-cased relative path -> (load index, path as written) of every active mod having it, lowest first.
    struct Hit {
        int mod = 0;
        std::string path;
    };
    std::map<std::string, std::vector<Hit>, std::less<>> index_;
    FileStamps stamps_;
    mutable std::shared_mutex mutex_;
};

// The process-wide registry.
Registry& registry();

}  // namespace sh::mods
