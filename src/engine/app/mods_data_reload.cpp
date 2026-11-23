// SPDX-License-Identifier: GPL-3.0-or-later
// `mods reload` of the data the game parsed: when a data file of an active mod
// changed, everything parsed from gamedata/ and the mods is read again between two frames, at the main frame's warm
// reset point. In play the current map then loads again in place (Pc_MapReload_Run), because the map, its scripts,
// its characters and the scene's models and textures were built from the old data; on the title nothing is loaded
// yet and the data simply applies from the next game on.

#include "engine/app/mods_data_reload.h"

#include <filesystem>
#include <string>
#include <vector>

#include "engine/app/map_info_tables.h"
#include "engine/app/pc_paths.h"
#include "engine/chara/game/chara_file_table.h"
#include "engine/config/gameplay_config.h"
#include "engine/game_state/session.h"
#include "engine/ids/game_numbers.h"
#include "engine/log/sh_log.h"
#include "engine/manifest/map_manifest.h"
#include "engine/mods/data_merge.h"
#include "engine/mods/data_reload.h"
#include "formats/common/json.h"
#include "render/scene/scene.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
void Inventory_GamedataReload(const char* gamedata_dir, const char* mods_dir);
void ControllerLayouts_GamedataReload(const char* gamedataDir, const char* modsDir);
int Pc_MapReload_Possible(void);
void Pc_MapReload_Run(void (*between)(void));
}

namespace {

bool g_pending = false;
bool g_wait_logged = false;

std::string mods_dir() {
    char buf[1024];
    return PcPaths_ExeFile(buf, sizeof(buf), "mods");
}

bool on_title() { return sh::game::session().game.game_state == sh::game::game_state::kMainMenu; }

// game.json again, only while every map keeps its number: the save and the session hold the current map by number.
void reload_game_json(const std::string& gamedata, const std::string& mods) {
    std::vector<std::string> warnings;
    const auto merged = sh::mods::read_data_file(gamedata, mods, "game.json", warnings);
    std::erase_if(warnings, [](const std::string& w) { return w.rfind("mods: ", 0) == 0; });
    sh::mods::emit_merge_warnings(warnings);
    if (!merged) {
        SH_LOG_WARN(GAME, "[RELOAD] game.json: %s (the maps stay as they were)", merged.error().c_str());
        return;
    }
    const auto doc = sh::json::parse(merged.value().text);
    const sh::json::Value* maps = doc ? doc.value().find("maps") : nullptr;
    if (maps == nullptr || !maps->is_array()) {
        SH_LOG_WARN(GAME, "[RELOAD] game.json from %s has no \"maps\" list (the maps stay as they were)",
                    merged.value().source().c_str());
        return;
    }
    const int known = MapManifest_Count();
    for (int i = 0; i < known; ++i) {
        const char* name = MapManifest_Name(i);
        const std::size_t at = static_cast<std::size_t>(i);
        const sh::json::Value* entry = at < maps->array().size() ? maps->array()[at].find("name") : nullptr;
        if (name != nullptr && (entry == nullptr || !entry->is_string() || entry->string() != name)) {
            SH_LOG_WARN(GAME,
                        "[RELOAD] game.json from %s moves or removes map %s: a map's number changes only after a "
                        "restart (the maps stay as they were)",
                        merged.value().source().c_str(), name);
            return;
        }
    }
    const int count = MapManifest_LoadText(merged.value().text.c_str());
    SH_LOG_INFO(GAME, "[RELOAD] game.json from %s: %d maps", merged.value().source().c_str(), count);
}

// Everything parsed from the data, read again. Runs between frames with no play state alive (after the exit to the
// title in play), so nothing still points into what it replaces.
void reload_data() {
    const std::string gamedata = PcPort_GetGameDataPath();
    const std::string mods = mods_dir();
    sh::mods::advance_data_epoch();
    sh::ids::refresh();
    Inventory_GamedataReload(gamedata.c_str(), mods.c_str());
    sh::chara::game::chara_files_reload(gamedata, mods);
    sh::app::reload_map_info(gamedata.c_str(), mods.c_str());
    ControllerLayouts_GamedataReload(gamedata.c_str(), mods.c_str());
    sh::gconfig::reload_active_config(gamedata, mods);
    reload_game_json(gamedata, mods);
    sh::scene::forget_assets();
    SH_LOG_INFO(GAME, "[RELOAD] game data read again (epoch %u): items, weapons, flags, characters, config, game.json, "
                      "models and textures",
                static_cast<unsigned>(sh::mods::data_epoch()));
}

}  // namespace

std::string sh::app::request_data_reload() {
    const sh::mods::ReloadChanges& changes = sh::mods::last_reload_changes();
    if (changes.data.empty()) {
        return "no data file changed";
    }
    for (const std::string& file : changes.data) {
        SH_LOG_INFO(GAME, "[RELOAD] changed %s: read again with the data", file.c_str());
    }
    g_pending = true;
    g_wait_logged = false;
    const std::string files = std::to_string(changes.data.size()) + " changed file(s)";
    if (Pc_MapReload_Possible()) {
        return files + ": read again at the next frame; the current map loads again in place (inventory, flags and "
                       "place kept as by a save)";
    }
    if (on_title()) {
        return files + ": read again at the next frame";
    }
    return files + ": read again as soon as play (outside events and menus) or the title is back, then the map "
                   "loads again in place";
}

extern "C" int Pc_DataReload_Due(void) {
    if (!g_pending) {
        return 0;
    }
    if (on_title() || Pc_MapReload_Possible()) {
        return 1;
    }
    if (!g_wait_logged) {
        g_wait_logged = true;
        SH_LOG_INFO(GAME, "[RELOAD] the data reload waits for play outside events and menus, or the title (game state "
                          "%d, system state %d)",
                    static_cast<int>(sh::game::session().game.game_state),
                    static_cast<int>(sh::game::session().system.flow.sys_state));
    }
    return 0;
}

extern "C" void Pc_DataReload_Run(void) {
    g_pending = false;
    if (Pc_MapReload_Possible()) {
        Pc_MapReload_Run(reload_data);
    } else {
        reload_data();
    }
}
