/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The loaded map: every map is data (gamedata/maps/<map>/). A map load
 * reads its disc tables (map_tables.h) and what it says about itself (map_description.h, the data kinds of
 * sh_maps, rooms, hooks, messages) into the running map (map_runtime.h).
 */
#include "engine/loader/active_map.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/app/pc_paths.h"
#include "engine/chara/game/harry_reaction_import.h"
#include "engine/loader/map_list.h"
#include "engine/log/sh_log.h"
#include "engine/maps/map_description.h"
#include "engine/maps/map_globals.h"
#include "engine/maps/map_runtime.h"
#include "engine/maps/map_table_data.h"
#include "engine/maps/map_tables.h"
#include "engine/maps/sh_maps.h"
#include "engine/runtime/map_hooks.h"
#include "engine/screens/staff_roll_clock.h"
#include "engine/vfs/vfs_c.h"
#include "engine/vfs/vfs_data_json.h"
#include "i18n/text_bridge.h"

extern "C" {
const char* PcPort_GetGameDataPath(void);
/* The clip tables of the new map, as the PS1 read them with its overlay (engine/chara/game/clip_tables.cpp). */
void ClipTables_MapLoaded(const char* mapName);
/* The story-character tables of a map with gamedata/maps/<map>/charas.json; 1 when
 * the map has a cast. */
int Map_CharasSet(const char* mapName);
}

namespace {

namespace maps = sh::maps;

// Which map is loaded ("" for none) and why the last load failed ("" when none did).
std::string g_loaded_map;
std::string g_load_error;

bool refuse(std::string reason)
{
    g_load_error = std::move(reason);
    SH_LOG_DEBUG(MAP, "map load refused: %s", g_load_error.c_str());
    return false;
}

/* Words of the overlay header (PS1 layout) the map's own data does not carry elsewhere. */
constexpr unsigned kDiscLoadableItems = 0x30;
constexpr unsigned kDiscPlayerAnims = 0x38;
constexpr unsigned kDiscPlayerAnimRows = 0x3C;
constexpr unsigned kDiscEffectSlotCount = 0x54;
constexpr unsigned kDiscBloodSplatCount = 0x5C;
constexpr unsigned kDiscEffectFire = 0x60;
constexpr unsigned kDiscEffectSmoke = 0x80;
constexpr unsigned kDiscEffectGlass = 0x8C;
constexpr unsigned kDiscEffectWater = 0x98;
constexpr unsigned kDiscEffectDrips = 0xA4;

bool data_file_exists(const std::string& relative)
{
    char path[1024];
    return ShVfs_FindDataFile(relative.c_str(), path, sizeof(path)) != 0;
}

/* gamedata/maps/<map>/tables.json and the overlay file it names: the map's disc tables. */
void tables_load(const std::string& map)
{
    maps::MapTables& tables = maps::map_tables();
    tables.clear();
    const maps::MapTableTypes* types = maps::map_table_types();
    const std::string relative = "maps/" + map + "/tables.json";
    // Base + every active mod's patch (symbols by "symbol"), in load order.
    const std::optional<std::string> text = ShVfs_ReadDataJsonText(relative.c_str());
    if (!text)
    {
        // Every map is data: without its registry every array would silently stay zero.
        SH_ERR("[MAPTABLES] %s: gamedata/maps/%s/tables.json is missing", map.c_str(), map.c_str());
        return;
    }
    if (types == nullptr || !tables.load_registry(map, relative, *text, *types))
    {
        return;
    }
    const std::string path = std::string(PcPort_GetGameDataPath()) + "/raw/" + tables.bin();
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        // Without the disc there is nothing to fill from: the arrays stay zero.
        SH_ERR("[MAPTABLES] %s: %s cannot be read: the game requires the disc import", map.c_str(),
               path.c_str());
        return;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // The arrays tables.json gives as readable data take it over the disc's bytes.
    auto data = maps::apply_table_data(*text, bytes, HarryReactions_TableNames());
    if (!data)
    {
        SH_ERR("[MAPTABLES] %s: %s: %s - the arrays fill from the disc", map.c_str(), relative.c_str(),
               data.error().c_str());
        tables.fill(bytes);
        return;
    }
    tables.fill(data.value().overlay);
}

/* A count of the overlay header, cut to what the engine keeps. */
int disc_count(const std::string& map, unsigned offset, int max, const char* what)
{
    std::int16_t count = 0;
    if (!maps::map_tables().header_s16(offset, &count))
    {
        SH_ERR("[MAP] %s: no disc header, the map has no %s", map.c_str(), what);
        return 0;
    }
    if (count < 0 || count > max)
    {
        SH_ERR("[MAP] %s: %d %s on the disc, the engine keeps %d", map.c_str(), count, what, max);
        return count < 0 ? 0 : max;
    }
    return count;
}

/* overlay.json, handlers.json and the arrays the overlay header points at. False (logged) when broken. */
bool map_begin(const std::string& map)
{
    maps::MapRuntime& rt = maps::map_runtime();
    rt.begin(map);

    const std::string overlay = "maps/" + map + "/overlay.json";
    const std::optional<std::string> overlay_text = ShVfs_ReadDataJsonText(overlay.c_str());
    if (!overlay_text || !maps::apply_overlay_description(rt, overlay, *overlay_text))
    {
        return false;
    }
    const std::string handlers = "maps/" + map + "/handlers.json";
    const std::optional<std::string> handlers_text = ShVfs_ReadDataJsonText(handlers.c_str());
    if (!handlers_text)
    {
        SH_ERR("[MAPOVERLAY] %s is missing", handlers.c_str());
        return false;
    }
    if (!maps::apply_event_handlers(rt, handlers, *handlers_text))
    {
        return false;
    }
    maps::MapTables& tables = maps::map_tables();
    if (tables.map() != map)
    {
        SH_ERR("[MAPOVERLAY] %s: gamedata/maps/%s/tables.json did not load; the map has no tables", map.c_str(),
               map.c_str());
        return false;
    }

    rt.set_loadable_items(static_cast<const std::uint8_t*>(tables.header_pointer(kDiscLoadableItems)));
    rt.set_player_anim_infos(tables.header_pointer(kDiscPlayerAnims));
    rt.set_player_anim_rows(static_cast<maps::PlayerAnimRow*>(tables.header_pointer(kDiscPlayerAnimRows)));
    // The effect parameter blocks a few maps keep (fire volume, smoke column, glass panes, water, drips).
    rt.set_effect_block(maps::EffectBlock::Fire, tables.header_pointer(kDiscEffectFire));
    rt.set_effect_block(maps::EffectBlock::Smoke, tables.header_pointer(kDiscEffectSmoke));
    rt.set_effect_block(maps::EffectBlock::Glass, tables.header_pointer(kDiscEffectGlass));
    rt.set_effect_block(maps::EffectBlock::Water, tables.header_pointer(kDiscEffectWater));
    rt.set_effect_block(maps::EffectBlock::Drips, tables.header_pointer(kDiscEffectDrips));
    SH_LOG("[MAPOVERLAY] %s: data-only map (no overlay library)", map.c_str());
    return true;
}

/* ---- rooms by position (rooms.json): the zone tables are arrays of the map's disc tables by name ---- */

class RoomsHost final : public maps::MapRoomsHost
{
public:
    std::span<const std::uint8_t> table(const std::string& map, const std::string& name) override
    {
        std::uint32_t bytes = 0;
        const void* data = maps::map_tables().map() == map ? maps::map_tables().array(name, &bytes) : nullptr;
        if (data == nullptr)
        {
            SH_ERR("[ROOMS] %s: the map has no table '%s' (or its table registry is not loaded)", map.c_str(),
                   name.c_str());
            return {};
        }
        return {static_cast<const std::uint8_t*>(data), bytes};
    }

    bool call_rule(const std::string& rule, std::int32_t x, std::int32_t z, long long* out) override
    {
        return maps::map_hooks().call_int(rule, x, z, out);
    }
};

void rooms_load(const std::string& map)
{
    static RoomsHost host;
    const std::string relative = "maps/" + map + "/rooms.json";
    // Base + every active mod's patch, in load order.
    if (const std::optional<std::string> text = ShVfs_ReadDataJsonText(relative.c_str()))
    {
        maps::map_runtime().load_rooms(relative, *text, host);
    }
}

/* ---- messages: gamedata/text/en (common.json + maps/<map>.json) ----
 * The base English text layer is the map's message table: the shared block 0..14 once in common.json, the map's
 * own lines from 15 on (text_bridge.cpp). The language and mod layers go on top later (lang_text.cpp). */

// Slots the language layer may read past a map's own table (i18n/localization.cpp); they hold "" so a stray
// index never reads outside the table.
constexpr std::size_t kMessageSlots = 176;

const char** map_messages(const std::string& map)
{
    // Kept for the whole run: a layer made by the language code may outlive the map, and the strings themselves
    // are owned by the English layer, which never changes.
    static std::vector<std::pair<std::string, std::vector<const char*>>> tables;
    for (auto& [name, table] : tables)
    {
        if (name == map)
        {
            return table.data();
        }
    }
    const int count = Pc_TextMapCount(map.c_str());
    if (count <= 0)
    {
        SH_ERR("[MESSAGES] %s: gamedata/text/en has no table of message keys for the map", map.c_str());
    }
    std::vector<const char*> table(std::max<std::size_t>(kMessageSlots, static_cast<std::size_t>(count)), "");
    for (int i = 0; i < count; i++)
    {
        const char* msg = Pc_TextMapMsg(map.c_str(), i, PC_TEXT_BASE);
        table[static_cast<std::size_t>(i)] = msg != nullptr ? msg : "";
    }
    tables.emplace_back(map, std::move(table));
    return tables.back().second.data();
}

/* The music event of hooks.lua. */
void hooks_music_event(bool first) { maps::map_hooks_bgm(first); }

/* ---- the data kinds of gamedata/maps/<map>/ (sh_maps) ---- */

void data_log(int level, const char* message)
{
    if (level == SH_MAPS_LOG_WARN)
    {
        SH_WARN("%s", message);
    }
    else
    {
        SH_LOG("%s", message);
    }
}

const char* source_name(int source)
{
    switch (source)
    {
        case SH_MAP_SOURCE_DISC: return "disc";
        case SH_MAP_SOURCE_FILE: return "file";
        case SH_MAP_SOURCE_MOD: return "mod";
        default: return "none";
    }
}

/* Header values, spawns, points, events, camera zones and collision into the running map. */
void data_load(const std::string& map)
{
    static bool configured = false;
    static char mods_dir[1024];
    if (!configured)
    {
        configured = true;
        PcPaths_ExeFile(mods_dir, sizeof(mods_dir), "mods");
        ShMapsConfig config{};
        config.gamedataDir = PcPort_GetGameDataPath();
        config.modsDir = mods_dir;
        config.log = data_log;
        ShMaps_Configure(&config);
    }
    static ShMapData data;
    ShMaps_Load(map.c_str(), &data);
    int missing = 0;
    for (int kind = 0; kind < SH_MAP_KIND_COUNT; kind++)
    {
        if (data.source[kind] == SH_MAP_SOURCE_NONE)
        {
            // Like a missing animation table, this means the disc import did not run.
            SH_ERR("[MAPDATA] %s: no %s data (gamedata/maps/%s/%s.json or the disc); the table stays empty - "
                   "import the disc image again (--reimport)",
                   map.c_str(), ShMaps_KindName(kind), map.c_str(), ShMaps_KindName(kind));
            missing++;
        }
    }
    SH_LOG("[MAPDATA] %s: header %s, spawns %s, points %s %d, events %s %d, cameras %s, collision %s%s", map.c_str(),
           source_name(data.source[SH_MAP_KIND_HEADER]), source_name(data.source[SH_MAP_KIND_SPAWNS]),
           source_name(data.source[SH_MAP_KIND_POINTS]), static_cast<int>(data.pointCount),
           source_name(data.source[SH_MAP_KIND_EVENTS]), static_cast<int>(data.eventCount),
           source_name(data.source[SH_MAP_KIND_CAMERAS]), source_name(data.source[SH_MAP_KIND_COLLISION]),
           missing > 0 ? " (DATA MISSING)" : "");
}

/* Everything else a map brings, once it is the current map. */
void map_complete(const std::string& map)
{
    maps::MapRuntime& rt = maps::map_runtime();
    // The story-character helpers work on a map with a cast; elsewhere they are not there, as its header had none.
    rt.set_has_cast(Map_CharasSet(map.c_str()) != 0);
    // The effect slot and blood splat counts are the overlay header's.
    rt.set_effect_slot_count(disc_count(map, kDiscEffectSlotCount, maps::kEffectSlots, "effect slots"));
    rt.set_blood_splat_count(disc_count(map, kDiscBloodSplatCount, maps::kBloodSplats, "blood splats"));
    // World objects: only a map that has the data file draws through the engine.
    rt.set_has_world_objects(data_file_exists("maps/" + map + "/world_objects.json"));
    // The hook script is loaded with the map: the music event runs during the load, long before the map's world
    // objects init.
    maps::map_hooks().load(map);
    if (maps::map_hooks().has("bgm"))
    {
        rt.set_music_event(hooks_music_event);
    }
    // Its zone rule runs in the hook script loaded just above.
    rooms_load(map);
    rt.set_map_messages(map_messages(map));
    data_load(map);
    rt.set_loaded(true);
}

/* The maps' run-long words by name (engine/maps/map_globals.h); their declarations are read on the first lookup. */
void* run_long_word(const char* name)
{
    maps::MapGlobals& globals = maps::map_globals();
    if (!globals.loaded())
    {
        globals.set_loaded();
        const std::optional<std::string> text = ShVfs_ReadDataJsonText("config/map_globals.json", PcPort_GetGameDataPath());
        std::string error;
        if (!text)
        {
            SH_ERR("[MapOverlay] config/map_globals.json is missing: the maps' run-long words are unknown");
        }
        else if (!globals.declare_from_json(*text, &error))
        {
            SH_ERR("[MapOverlay] config/map_globals.json: %s", error.c_str());
        }
    }
    return globals.find(name);
}

}  // namespace

namespace sh::loader {

bool load_active_map(int map_index)
{
    if (map_index < 0 || map_index >= map_count())
    {
        return refuse("There is no map number " + std::to_string(map_index) + " in gamedata/game.json");
    }
    const std::string map = map_name(map_index);
    if (!data_file_exists("maps/" + map + "/overlay.json"))
    {
        return refuse("The data of " + map + " is missing (gamedata/maps/" + map +
                      "/overlay.json); import the disc again or reinstall the base game data");
    }
    g_loaded_map = map;
    ClipTables_MapLoaded(map.c_str());
    tables_load(map);
    if (!map_begin(map))
    {
        g_loaded_map.clear();
        return refuse("The data of " + map + " (gamedata/maps/" + map + "/) cannot be read");
    }
    map_complete(map);
    return true;
}

const char* active_map_name() { return g_loaded_map.empty() ? nullptr : g_loaded_map.c_str(); }

const char* active_map_error() { return g_load_error.c_str(); }

void* active_map_symbol(const char* name)
{
    if (name == nullptr || g_loaded_map.empty())
    {
        return nullptr;
    }
    const std::string_view wanted = name;
    if (wanted == "map_points")
    {
        return maps::map_runtime().points();
    }
    if (wanted == "effect_slots")
    {
        return maps::map_runtime().effect_slots();
    }
    if (wanted == "staff_roll_clock")
    {
        return &sh::screens::staff_roll_clock();
    }
    if (void* table = MapTables_DataSymbol(name))
    {
        return table;
    }
    if (void* word = run_long_word(name))
    {
        return word;
    }
    return nullptr;
}

}  // namespace sh::loader
