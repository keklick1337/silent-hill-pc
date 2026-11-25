#pragma once

// Where the data of a map overlay comes from when the game loads it: gamedata/maps/<map>/<kind>.json (written
// from the disc first when it is missing) with every mod's patch merged over it in load order
// (engine/mods/data_merge.h), else the disc itself (the game file VIN/MAPn_Snn.BIN), else nothing and the game
// keeps the values compiled into the overlay.
// The C face for the game is engine/maps/sh_maps.h.

#include "formats/convert/mapdata/mapdata.h"

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sh::maps {

using LogFn = std::function<void(int level, const std::string& message)>;  // level: 0 info, 1 warning

struct StoreConfig {
    std::filesystem::path gamedata_dir;
    std::filesystem::path mods_dir;  // <mods_dir>/<mod>/maps/<map>/<kind>.json; empty: no mods
    LogFn log;
};

enum class Source : std::uint8_t { None, Disc, File, Mod };

struct KindSource {
    Source source = Source::None;
    std::string path;  // the file read, or the game file "VIN/MAPn_Snn.BIN"
    std::string mod;   // mod id for Source::Mod
};

struct MapLoad {
    sh::convert::mapdata::MapData data;
    std::array<KindSource, sh::convert::mapdata::kAllKinds.size()> kinds;
};

class MapStore {
public:
    explicit MapStore(StoreConfig config);

    // Never fails: problems are logged with the file, line and field, and the disc values are used instead.
    // Files are read on every call: the PS1 read the overlay from the disc on every load as well.
    MapLoad load(std::string_view map);

private:
    struct ModDir {
        std::string id;
        bool gameplay = false;
        std::filesystem::path root;
    };

    // A data file of the map: gamedata's copy (when `with_base`) and every mod's copy (when `with_mods`)
    // merged in load order (engine/mods/data_merge.h); nothing when no file has it.
    struct Merged {
        std::string text;
        std::string path;                  // the file read, or the merged files, for messages
        std::vector<const ModDir*> mods;   // the mods whose copy took part, in load order
    };
    std::optional<Merged> read_merged(const std::filesystem::path& relative, bool with_base,
                                      bool with_mods = true) const;

    void log(int level, const std::string& message) const;
    void discover_mods();

    StoreConfig config_;
    bool mods_discovered_ = false;
    std::vector<ModDir> mods_;  // highest priority first
};

}  // namespace sh::maps
