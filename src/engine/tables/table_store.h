#pragma once

// Where the game's behaviour tables come from: a mod, gamedata/tables/, or the disc
// (gamedata/raw/). The C face for the game is engine/tables/sh_tables.h.
//
// The disc is the reference. Each map overlay carries its own copy of a table, and a few copies differ (DAHLIA in
// the later maps, CYBIL in MAP6_S01, KAUFMANN in MAP5_S02). The table of the first source is <name>.json; a copy
// that differs from it is <name>@<MAPn_Snn>.json and replaces it while that overlay is loaded.

#include "engine/tables/anim_table.h"
#include "engine/tables/sh_tables.h"
#include "formats/common/status.h"

#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sh::tables {

using LogFn = std::function<void(int level, const std::string& message)>;

struct StoreConfig {
    std::filesystem::path gamedata_dir;  // tables are <gamedata_dir>/tables/<group>/<name>.json, the disc <gamedata_dir>/raw/
    std::filesystem::path mods_dir;      // <mods_dir>/<mod>/tables/<group>/<name>.json; empty: no mods
    bool overwrite = false;              // import_overwrite = 1: rewrite gamedata/tables from the disc
    std::vector<ShAnimFunc> funcs;
    LogFn log;
};

// import_overwrite from a config.cfg text ("key = value", '#' comments). Unknown values keep
// the default and add a line to `warnings`. There is no `import_tables` key: the tables always come
// from gamedata/tables/ and the disc; a config line saying otherwise is a warning.
void apply_config_text(StoreConfig& config, std::string_view text, std::vector<std::string>& warnings);

// "tables/anim/<name>.json", or "tables/anim/<name>@<overlay>.json": the path under gamedata/ and inside a mod.
std::filesystem::path anim_table_path(const ShAnimTableInfo& info, std::string_view overlay = {});

// The values a table has when nobody edited it: the disc copies in gamedata/raw/. Nothing is compiled into the
// game.
struct DefaultAnimTables {
    AnimTable base;              // the disc copy of sources[0]
    std::vector<std::pair<std::string, AnimTable>> variants;  // overlay -> disc copy that differs from `base`
    std::vector<std::string> notes;                           // raw/ problems
    bool disc_failed = false;  // the disc values could not be read; the notes say why

    // The variant of `overlay`, or `base`.
    const AnimTable& for_overlay(std::string_view overlay) const;
};

DefaultAnimTables default_anim_tables(const std::filesystem::path& gamedata_dir, const ShAnimTableInfo& info,
                                      std::span<const ShAnimFunc> funcs);

struct WriteReport {
    std::vector<std::string> written;  // paths relative to gamedata/
    std::vector<std::string> kept;
    std::vector<std::string> notes;    // DefaultAnimTables::notes
};

// Writes <name>.json and one <name>@<overlay>.json per differing disc copy into <gamedata_dir>/tables/anim/.
// An existing file is kept unless `overwrite`. Each file is written to a temporary name and renamed.
// Fails without writing when the disc values could not be read: the game requires the import.
Result<WriteReport> write_default_anim_tables(const std::filesystem::path& gamedata_dir, const ShAnimTableInfo& info,
                                              std::span<const ShAnimFunc> funcs, bool overwrite);

class TableStore {
public:
    explicit TableStore(StoreConfig config);

    struct AnimLoad {
        std::vector<ShAnimEntry> entries;  // info.count entries
        bool from_file = false;
    };

    // The table while `overlay` ("MAP5_S02") is loaded; empty: no overlay. Never returns less than info.count
    // entries: a missing or broken file is logged as an error and the disc values fill the array (zeros when
    // even raw/ is absent: the game requires the disc import). Results are kept: read once per run.
    AnimLoad load_anim(const ShAnimTableInfo& info, std::string_view overlay = {});

    bool gameplay_changed() const { return gameplay_changed_; }

private:
    struct ModDir {
        std::string id;
        bool gameplay = false;
        std::filesystem::path root;
    };

    void note(const std::string& message) const;
    void warn(const std::string& message) const;
    void error(const std::string& message) const;
    void discover_mods();
    const DefaultAnimTables& defaults_for(const ShAnimTableInfo& info);
    void ensure_files(const ShAnimTableInfo& info);
    AnimLoad resolve(const ShAnimTableInfo& info, std::string_view overlay);

    StoreConfig config_;
    bool mods_discovered_ = false;
    bool announced_ = false;
    bool gameplay_changed_ = false;
    std::vector<ModDir> mods_;  // highest priority first
    std::map<std::string, DefaultAnimTables> defaults_;  // by table name
    std::map<std::string, bool> files_checked_;           // by table name
    std::map<std::string, AnimLoad> loaded_;              // by "<name>@<overlay>"
};

}  // namespace sh::tables
