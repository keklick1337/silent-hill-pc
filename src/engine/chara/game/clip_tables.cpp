// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/chara/game/clip_tables.h"

#include <array>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/chara/game/chara_link.h"
#include "engine/entities/game/model_runtime.h"
#include "engine/log/sh_log.h"
#include "engine/tables/anim_table_registry.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
// The engine's playbacks (model_runtime.cpp), under the names the map tables give them.
void Anim_BlendLinear(sh::entities::ModelRecord* model, void* anm_header, sh::entities::BoneCoord* bones,
                      sh::entities::ClipInfoRecord* clip);
void Anim_PlaybackOnce(sh::entities::ModelRecord* model, void* anm_header, sh::entities::BoneCoord* bones,
                       sh::entities::ClipInfoRecord* clip);
void Anim_PlaybackLoop(sh::entities::ModelRecord* model, void* anm_header, sh::entities::BoneCoord* bones,
                       sh::entities::ClipInfoRecord* clip);
}

namespace sh::chara::game {
namespace {

struct Table {
    const ShAnimTableInfo* info;
    std::vector<ClipInfo> clips;
    bool per_map;  // filled again with every map load
};

std::vector<Table>& tables() {
    static std::vector<Table> t = [] {
        std::vector<Table> out;
        for (unsigned i = 0; i < g_AnimTableInfoCount; ++i) {
            const ShAnimTableInfo* info = g_AnimTableInfos[i];
            // The Puppet Nurse's two tables belong to its variants, which the map loads do not touch.
            const bool per_map = info != &g_PuppetNurseAAnimTable && info != &g_PuppetNurseBAnimTable;
            out.push_back({info, std::vector<ClipInfo>(info->count), per_map});
        }
        return out;
    }();
    return t;
}

void log_tables(int level, const char* message) {
    if (level == SH_TABLES_LOG_WARN) {
        SH_WARN("%s", message);
    } else if (level == SH_TABLES_LOG_ERROR) {
        // A missing or broken table file means the disc import did not run.
        SH_ERR("%s", message);
    } else {
        SH_LOG("%s", message);
    }
}

void configure() {
    static bool configured = false;
    static char mods_dir[1024];
    static char config_path[1024];
    if (configured) {
        return;
    }
    configured = true;
    PcPaths_ExeFile(mods_dir, sizeof(mods_dir), "mods");
    PcPaths_ExeFile(config_path, sizeof(config_path), "config.cfg");
    ShTablesConfig config{};
    config.gamedataDir = PcPort_GetGameDataPath();
    config.modsDir = mods_dir;
    config.configPath = config_path;
    config.funcs = g_AnimTableFuncs;
    config.funcCount = g_AnimTableFuncCount;
    config.log = log_tables;
    ShTables_Configure(&config);
}

void fill(Table& table, const char* overlay) {
    std::array<ShAnimEntry, ANIM_TABLE_MAX_ENTRIES> entries{};
    if (table.info->count > entries.size()) {
        SH_WARN("[TABLES] %s has %d entries, more than %d", table.info->symbol, table.info->count,
                ANIM_TABLE_MAX_ENTRIES);
        return;
    }
    configure();
    ShTables_LoadAnimForOverlay(table.info, overlay, entries.data());
    fill_clip_entries(table.clips, std::span<const ShAnimEntry>(entries.data(), table.info->count),
                      engine_clip_playbacks());
}

}  // namespace

ClipPlaybacks engine_clip_playbacks() {
    return {reinterpret_cast<ClipPlayback>(&Anim_BlendLinear), reinterpret_cast<ClipPlayback>(&Anim_PlaybackOnce),
            reinterpret_cast<ClipPlayback>(&Anim_PlaybackLoop)};
}

void clip_tables_init() {
    for (Table& t : tables()) {
        fill(t, nullptr);
    }
}

void clip_tables_map_loaded(const char* map) {
    std::string overlay;
    for (const char* c = map; c != nullptr && *c != '\0' && overlay.size() < 15; ++c) {
        overlay += static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    }
    for (Table& t : tables()) {
        if (t.per_map) {
            fill(t, overlay.c_str());
        }
    }
}

ClipTableRef clip_table(std::string_view name) {
    for (Table& t : tables()) {
        if (name == t.info->name) {
            return {t.clips.data(), static_cast<int>(t.clips.size())};
        }
    }
    return {};
}

// The character hosts' link to the tables (chara_link.h).
namespace link {

ClipTableRef anim_table(const char* name) { return name != nullptr ? clip_table(name) : ClipTableRef{}; }
ClipPlayback clip_playback_blend() { return engine_clip_playbacks().blend; }
ClipPlayback clip_playback_once() { return engine_clip_playbacks().once; }
ClipPlayback clip_playback_loop() { return engine_clip_playbacks().loop; }

}  // namespace link

}  // namespace sh::chara::game

// The boot's and the map loader's calls (game_main.cpp, active_map.cpp).
extern "C" void ClipTables_Init(void) { sh::chara::game::clip_tables_init(); }
extern "C" void ClipTables_MapLoaded(const char* map) { sh::chara::game::clip_tables_map_loaded(map); }
