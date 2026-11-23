// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/loader/map_list.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "engine/app/dialog.h"
#include "engine/config/pc_config.h"
#include "engine/game_state/game_state_map_overlay.h"
#include "engine/log/sh_log.h"
#include "engine/manifest/map_manifest.h"

// The debug NPC spawner's slot marks (features/cheats).
extern "C" void Pc_NpcDebugSpawnClearAll(void);

namespace sh::loader {

namespace gs = sh::game_state;

namespace {

[[noreturn]] void stop_without_map(int index, const std::string& reason) {
    const std::string name = map_name(index);
    SH_LOG_ERROR(MAP, "map %s (number %d) could not be loaded: %s", name.c_str(), index, reason.c_str());
    const std::string message = "Map " + name + " could not be loaded:\n\n" + reason +
                                "\n\nThe game cannot continue without it. The maps are data in\n"
                                "gamedata/maps/ next to the executable (the disc import and the base game data).";
    ShDialog_Show(1, "Silent Hill - map data missing", message.c_str());
    std::exit(EXIT_FAILURE);
}

}  // namespace

int map_count() { return MapManifest_Count(); }

const char* map_name(int index) {
    const char* name = MapManifest_Name(index);
    return name != nullptr ? name : "unknown";
}

const char* map_description(int index) {
    const char* description = MapManifest_Description(index);
    return description != nullptr ? description : "";
}

int find_map(const char* name) {
    if (name == nullptr) {
        return -1;
    }
    for (int i = 0, count = map_count(); i < count; ++i) {
        if (std::strcmp(name, map_name(i)) == 0) {
            return i;
        }
    }
    return -1;
}

void load_map(int index) {
    gs::set_current_map_index(index);
    if (!gs::map_overlay_install(index)) {
        stop_without_map(index, gs::map_overlay_error());
    }
    SH_LOG_INFO(MAP, "map %s (%s) loaded: number %d, type %d", map_name(index), map_description(index), index,
                gs::map_overlay_map_type());
    // The characters of the previous map are gone, and the spawner's marks of them with them.
    Pc_NpcDebugSpawnClearAll();
}

void load_start_map() {
    const int maps = MapManifest_Load();
    if (maps > 0) {
        SH_LOG_INFO(CONFIG, "game.json: %d maps", maps);
    } else {
        SH_LOG_ERROR(CONFIG, "gamedata/game.json lists no maps");
    }
    int index = find_map(g_PcConfig.mapName);
    if (index < 0) {
        if (g_PcConfig.mapName[0] != '\0') {
            SH_LOG_WARN(CONFIG, "map '%s' is not a known map; starting at %s", g_PcConfig.mapName,
                        map_name(gs::kMapIndexFirst));
        }
        index = gs::kMapIndexFirst;
    }
    load_map(index);
}

}  // namespace sh::loader
