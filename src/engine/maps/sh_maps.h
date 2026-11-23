#ifndef SH_ENGINE_MAPS_SH_MAPS_H
#define SH_ENGINE_MAPS_SH_MAPS_H

/* C face of sh_maps: the data of a map overlay from gamedata/maps/<map>/, for the game code that copies it
 * into the overlay's structures. */

#include "formats/convert/mapdata/map_records.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    SH_MAPS_LOG_INFO = 0,
    SH_MAPS_LOG_WARN = 1
};

/* Kinds of data, one JSON file each: maps/<map>/<name>.json. */
enum
{
    SH_MAP_KIND_HEADER = 0,
    SH_MAP_KIND_SPAWNS,
    SH_MAP_KIND_POINTS,
    SH_MAP_KIND_EVENTS,
    SH_MAP_KIND_CAMERAS,
    SH_MAP_KIND_COLLISION,
    SH_MAP_KIND_COUNT
};

enum
{
    SH_MAP_SOURCE_NONE = 0, /* nothing readable: keep the values compiled into the overlay */
    SH_MAP_SOURCE_DISC = 1, /* gamedata/raw/VIN/MAPn_Snn.BIN (the JSON file was missing or broken) */
    SH_MAP_SOURCE_FILE = 2, /* gamedata/maps/<map>/<kind>.json */
    SH_MAP_SOURCE_MOD  = 3  /* <mods>/<mod>/maps/<map>/<kind>.json */
};

typedef struct ShMapsConfig
{
    const char* gamedataDir;
    const char* modsDir; /* NULL or "" for none */
    void (*log)(int level, const char* message); /* NULL: stderr */
} ShMapsConfig;

typedef struct ShMapData
{
    uint8_t         source[SH_MAP_KIND_COUNT];
    char            path[SH_MAP_KIND_COUNT][160]; /* what was read, for the log */
    ShMapHeaderInfo header;
    ShMapSpawns     spawns;
    uint16_t        pointCount;
    uint16_t        eventCount;
    ShMapPoint      points[SH_MAP_POINTS_MAX];
    ShMapEvent      events[SH_MAP_EVENTS_MAX];
    ShMapCamera     cameras[SH_MAP_CAMERA_PATHS];
    ShMapCollision  collision[SH_MAP_COLLISION_COUNT];
} ShMapData;

/* Sets where the data comes from. A second call replaces the setup. */
void ShMaps_Configure(const ShMapsConfig* config);

/* Fills *out for the overlay `map` ("map1_s02"). Problems are logged; a kind without a source keeps
 * source[kind] = SH_MAP_SOURCE_NONE. Returns the number of kinds that have a source. */
int ShMaps_Load(const char* map, ShMapData* out);

/* The ambient sound bank index of the header of `map` (as ShMaps_Load would read it), -1 when the map has no header.
 * Reads the data without making it the loaded map's. */
int ShMaps_AmbientAudioOf(const char* map);

/* "spawns" */
const char* ShMaps_KindName(int kind);

/* Story-driven spawns: one selected write of the map.json
 * "spawn_overrides" section. Names, not numbers: the game resolves them through its registries. */
typedef struct ShMapSpawnWrite
{
    const char* chara;              /* e_Chara name ("GreyChild") */
    int         slot;               /* index into the map's spawn table */
    const char* flags[8];           /* e_SpawnFlags names ("none", "spawn_0".."spawn_4") */
    int         flagCount;
} ShMapSpawnWrite;

/* Event-flag lookup by name for the selector; nonzero means the flag is set. */
typedef int (*ShMapsSpawnFlagGet)(void* user, const char* name);

/* Runs the generic selector over the spawn_overrides of the LAST ShMaps_Load: every set entry of
 * every override whose condition matches, in file order (a later entry wins). Fills out up to
 * `capacity` entries and returns the total selected count; out may be NULL to just count. The
 * strings point into the store's data and live until the next ShMaps_Load. */
int ShMaps_SpawnOverridesSelect(int difficulty, ShMapsSpawnFlagGet flag_get, void* user,
                                ShMapSpawnWrite* out, int capacity);

/* Spawn variants: the choice the generic selector made over
 * the spawn_variants of the LAST ShMaps_Load. The adapter sets setFlags, copies rows over the map's
 * spawn table and moves the killed-enemy words: on apply active <- load, while the map runs
 * save <- active. Pointers live until the next ShMaps_Load. */
typedef struct ShMapSpawnVariantPick
{
    const char*       variant;          /* the variant's name, for the log */
    const ShMapSpawn* rows;             /* SH_MAP_SPAWN_GROUPS * SH_MAP_SPAWN_SLOTS rows, row = group * 16 + slot */
    int               enemyStateActive; /* the map's own save-record enemy_states word */
    int               enemyStateLoad;
    int               enemyStateSave;
    const char*       setFlags[8];      /* event flags the choice sets */
    int               setFlagCount;
} ShMapSpawnVariantPick;

/* 1 and *out filled when the map has spawn variants with readable rows and a choice matched; else 0. */
int ShMaps_SpawnVariantSelect(int difficulty, ShMapsSpawnFlagGet flag_get, void* user, ShMapSpawnVariantPick* out);

/* Character name -> e_Chara number (-1 unknown). */
int ShMaps_CharaNumberOf(const char* name);

#ifdef __cplusplus
}
#endif

#endif
