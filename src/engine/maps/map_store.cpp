#include "engine/maps/map_store.h"

#include "engine/mods/data_merge.h"
#include "engine/mods/mods.h"
#include "formats/convert/gamefiles/game_files.h"
#include "formats/convert/mapdata/mapdata_files.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

namespace sh::maps {

namespace fs = std::filesystem;
namespace md = sh::convert::mapdata;

MapStore::MapStore(StoreConfig config) : config_(std::move(config)) {}

void MapStore::log(int level, const std::string& message) const {
    if (config_.log) {
        config_.log(level, "[MAPDATA] " + message);
    } else {
        std::fprintf(stderr, "%s[MAPDATA] %s\n", level ? "warning: " : "", message.c_str());
    }
}

void MapStore::discover_mods() {
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
        log(1, "mods: " + w);
    }
    for (auto it = found.rbegin(); it != found.rend(); ++it) {
        mods_.push_back({it->id, it->gameplay, it->root});
    }
}

MapLoad MapStore::load(std::string_view map) {
    MapLoad result;
    result.data.map = std::string(map);
    const md::MapLayout* layout = md::find_layout(map);
    if (!layout) {
        log(1, std::string(map) + ": not a map overlay of this game version");
        return result;
    }
    discover_mods();

    // The disc first: it fills a missing gamedata/maps file and stands in for a broken one.
    bool have_disc = false;
    md::MapData disc;
    const std::string raw_name = md::overlay_path(map);
    if (auto overlay = sh::convert::gamefiles::read_game_file(config_.gamedata_dir, raw_name); !overlay) {
        log(1, std::string(map) + ": the game file " + raw_name + " cannot be read");
    } else if (auto decoded = md::decode_overlay(*overlay, *layout); !decoded) {
        log(1, std::string(map) + ": " + raw_name + ": " + decoded.error());
    } else {
        disc = std::move(decoded.value());
        have_disc = true;
    }

    // The unified scene view map.json fills all six kinds at once. With mods' map.json patches
    // (merged over the base one in load order) the merged scene is the map; otherwise a base
    // map.json stands in for every kind file no mod has, and the six separate files stop being read.
    const fs::path scene_relative = fs::path("maps") / map / "map.json";
    auto read_scene = [&](const Merged& file, md::MapData& into) {
        std::vector<std::string> warnings;
        into = have_disc ? disc : md::MapData{};
        into.map = std::string(map);
        const Result<void> read = md::read_unified_json(file.text, *layout, into, warnings);
        for (const std::string& w : warnings) {
            log(1, file.path + ": " + w);
        }
        if (read) {
            return true;
        }
        log(1, file.path + ": " + read.error() + "; the separate kind files are used instead");
        return false;
    };

    bool have_scene = false;
    md::MapData scene;
    if (const std::optional<Merged> merged = read_merged(scene_relative, true)) {
        if (!merged->mods.empty()) {
            md::MapData modded;
            if (read_scene(*merged, modded)) {
                result.data = std::move(modded);
                for (KindSource& src : result.kinds) {
                    src.source = Source::Mod;
                    src.path = scene_relative.generic_string();
                    src.mod = merged->mods.back()->id;
                }
                for (const ModDir* m : merged->mods) {
                    if (!m->gameplay) {
                        log(1, "mod " + m->id + " changes " + scene_relative.generic_string() +
                                   " (game data) but does not declare \"gameplay\": true in mod.json");
                    }
                }
                return result;
            }
        }
        // A base map.json: each kind comes from it unless a mod has that kind file.
        if (merged->mods.empty()) {
            have_scene = read_scene(*merged, scene);
        } else if (const std::optional<Merged> base = read_merged(scene_relative, true, false)) {
            have_scene = read_scene(*base, scene);
        }
    }

    for (std::size_t k = 0; k < md::kAllKinds.size(); ++k) {
        const md::Kind kind = md::kAllKinds[k];
        const fs::path relative = md::map_file_path(map, kind);
        std::error_code ec;
        const fs::path in_gamedata = config_.gamedata_dir / relative;
        // With a scene file present the six kind files are derived views: the store
        // does not create or read them.
        if (have_disc && !have_scene && !fs::exists(in_gamedata, ec)) {
            if (const Result<void> w = md::write_file_atomic(in_gamedata, md::write_json(disc, kind)); w) {
                log(0, "wrote " + relative.generic_string() + " from " + raw_name);
            } else {
                log(1, w.error());
            }
        }

        // The base kind file (unless a map.json stands in for it) + every mod's patch, in load order.
        const std::optional<Merged> chosen = read_merged(relative, !have_scene);
        const ModDir* mod = chosen && !chosen->mods.empty() ? chosen->mods.back() : nullptr;

        KindSource& src = result.kinds[k];
        if (!chosen && have_scene) {
            // From the base map.json: one source for every kind.
            switch (kind) {
                case md::Kind::Header: result.data.header = scene.header; break;
                case md::Kind::Spawns: result.data.spawns = scene.spawns; break;
                case md::Kind::Points: result.data.points = scene.points; break;
                case md::Kind::Events: result.data.events = scene.events; break;
                case md::Kind::Cameras: result.data.cameras = scene.cameras; break;
                case md::Kind::Collision: result.data.collision = scene.collision; break;
            }
            src.source = Source::File;
            src.path = scene_relative.generic_string();
            continue;
        }
        if (chosen) {
            std::vector<std::string> warnings;
            md::MapData parsed = have_disc ? disc : md::MapData{};
            parsed.map = std::string(map);
            const Result<void> read = md::read_json(chosen->text, kind, *layout, parsed, warnings);
            for (const std::string& w : warnings) {
                log(1, chosen->path + ": " + w);
            }
            if (read) {
                switch (kind) {
                    case md::Kind::Header: result.data.header = parsed.header; break;
                    case md::Kind::Spawns: result.data.spawns = parsed.spawns; break;
                    case md::Kind::Points: result.data.points = parsed.points; break;
                    case md::Kind::Events: result.data.events = parsed.events; break;
                    case md::Kind::Cameras: result.data.cameras = parsed.cameras; break;
                    case md::Kind::Collision: result.data.collision = parsed.collision; break;
                }
                src.source = mod ? Source::Mod : Source::File;
                src.path = mod ? chosen->path : relative.generic_string();
                if (mod) {
                    src.mod = mod->id;
                }
                for (const ModDir* m : chosen->mods) {
                    if (!m->gameplay) {
                        log(1, "mod " + m->id + " changes " + relative.generic_string() +
                                   " (game data) but does not declare \"gameplay\": true in mod.json");
                    }
                }
                continue;
            }
            log(1, chosen->path + ": " + read.error() +
                       (have_disc ? "; using the disc values" : "; the disc is not readable either"));
        }
        if (have_disc) {
            switch (kind) {
                case md::Kind::Header: result.data.header = disc.header; break;
                case md::Kind::Spawns: result.data.spawns = disc.spawns; break;
                case md::Kind::Points: result.data.points = disc.points; break;
                case md::Kind::Events: result.data.events = disc.events; break;
                case md::Kind::Cameras: result.data.cameras = disc.cameras; break;
                case md::Kind::Collision: result.data.collision = disc.collision; break;
            }
            src.source = Source::Disc;
            src.path = raw_name;
        }
    }
    /* The overlay's alternative spawn tables: a base map.json's (it starts from the disc's, so an
     * older file without the section keeps them), else the disc's. A mod's map.json returned above. */
    result.data.spawn_variant_rows = have_scene ? scene.spawn_variant_rows : disc.spawn_variant_rows;
    /* Hand data: maps/<map>/spawn_overrides.json: the base
     * file + every mod's patch (entries by "id"; entries without one are appended), in load order.
     * The entries apply IN ADDITION to any spawn_overrides a map.json carried (appended after them). */
    {
        const fs::path overrides_relative = fs::path("maps") / map / "spawn_overrides.json";
        if (const std::optional<Merged> chosen = read_merged(overrides_relative, true)) {
            std::vector<std::string> warnings;
            const Result<void> read = md::read_spawn_overrides_file(chosen->text, result.data, warnings);
            for (const std::string& w : warnings) {
                log(1, chosen->path + ": " + w);
            }
            if (read) {
                log(0, "spawn overrides: " + chosen->path);
                for (const ModDir* m : chosen->mods) {
                    if (!m->gameplay) {
                        log(1, "mod " + m->id + " changes " + overrides_relative.generic_string() +
                                   " (game data) but does not declare \"gameplay\": true in mod.json");
                    }
                }
            } else {
                log(1, chosen->path + ": " + read.error() + "; the spawn overrides are not applied");
            }
        }
    }
    return result;
}

std::optional<MapStore::Merged> MapStore::read_merged(const fs::path& relative, bool with_base, bool with_mods) const {
    std::vector<mods::DataLayer> layers;
    std::vector<const ModDir*> owners;
    std::error_code ec;
    if (with_mods) {
        for (auto it = mods_.rbegin(); it != mods_.rend(); ++it) {  // mods_ is highest first; merge lowest first
            if (fs::is_regular_file(it->root / relative, ec)) {
                layers.push_back({it->id, it->root / relative, it->root, it->gameplay});
                owners.push_back(&*it);
            }
        }
    }
    std::vector<std::string> warnings;
    Result<mods::MergedFile> merged = mods::merge_layers(
        relative.generic_string(), with_base ? config_.gamedata_dir / relative : fs::path(), layers, warnings);
    for (const std::string& w : warnings) {
        log(1, w);
    }
    if (!merged) {
        if (!merged.error().starts_with("no file")) {
            log(1, merged.error());
        }
        return std::nullopt;
    }
    mods::emit_merge_trace(merged.value());
    Merged out;
    out.text = std::move(merged.value().text);
    out.path = merged.value().merged() ? merged.value().source() : merged.value().path.generic_string();
    for (const mods::DataLayer& layer : merged.value().mods) {
        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (layers[i].file == layer.file) {
                out.mods.push_back(owners[i]);
            }
        }
    }
    return out;
}

}  // namespace sh::maps
