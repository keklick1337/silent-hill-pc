#ifndef SH_ENGINE_TABLES_SH_TABLES_H
#define SH_ENGINE_TABLES_SH_TABLES_H

/* C face of sh_tables: behaviour tables that the game reads from gamedata/tables/.
 * Only plain C types, so game C code and the C++ tools share the same declarations.
 * File format: engine/tables/anim_table.h; background: docs/FORMATS.md. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One `s_AnimInfo` entry without its function pointer; `kind` chooses the playback function (`e_AnimKind`).
 * Same layout as the game's animation table rows. */
typedef struct ShAnimEntry
{
    uint8_t kind;               /* 0 blend_linear, 1 playback_once, 2 playback_loop, 3 none (NULL) */
    uint8_t status;
    int8_t  hasVariableDuration;
    uint8_t linkStatus;
    int32_t durationQ12;        /* keyframes per second in Q12; with hasVariableDuration, ShAnimFunc::value */
    int16_t startKeyframeIdx;
    int16_t endKeyframeIdx;
} ShAnimEntry;

/* Where a table lies on the disc: the map overlay (its game path, formats/convert/gamefiles/game_files.h) and its PSX address. */
typedef struct ShAnimSource
{
    const char* file;
    uint32_t    va;
} ShAnimSource;

/* A table of the game: its files and its place on the disc. No values are compiled in: they
 * come from gamedata/tables/anim/, written by the import from the disc. */
typedef struct ShAnimTableInfo
{
    const char*         name;      /* file name without ".json": "cat" -> tables/anim/cat.json */
    const char*         symbol;    /* array in the game: "CAT_ANIM_INFOS" */
    uint16_t            count;     /* entries of the game array */
    uint16_t            fileCount; /* entries kept in the file; the entries past it are zero (no status
                                      * reaches them: FLOATSTINGER's tail is neighbouring rodata) */
    const ShAnimSource* sources;   /* copies on the disc; sources[0] is the copy <name>.json is written from */
    uint16_t            sourceCount;
    uint16_t            linkLimit; /* links must be below this or 255; `count` when the statuses index this array */
} ShAnimTableInfo;

/* A function that a variable-duration entry can name in "duration_func". `value` is what the entry's
 * duration field holds until the game resolves it (the PSX address, as in the extracted tables). */
typedef struct ShAnimFunc
{
    const char* name;
    int32_t     value;
} ShAnimFunc;

enum
{
    SH_TABLES_LOG_INFO = 0,
    SH_TABLES_LOG_WARN = 1,
    SH_TABLES_LOG_ERROR = 2
};

typedef struct ShTablesConfig
{
    const char*       gamedataDir; /* tables are read from and created in <gamedataDir>/tables/ */
    const char*       modsDir;     /* <modsDir>/<mod>/tables/... replace them; NULL or "" for none */
    const char*       configPath;  /* config.cfg: import_tables, import_overwrite; NULL for the defaults */
    const ShAnimFunc* funcs;
    uint32_t          funcCount;
    void (*log)(int level, const char* message); /* NULL: stderr */
} ShTablesConfig;

/* Sets where tables come from. Called before the first ShTables_LoadAnim; a second call replaces the setup. */
void ShTables_Configure(const ShTablesConfig* config);

/* Fills out[0 .. info->count) with the table: from a mod's file, else from gamedata/tables/anim/<name>.json
 * (written from the disc's game files first when it is missing), else from the disc copy in the game files.
 * A missing or broken file is an error reported through the log: the game requires the disc import;
 * the disc values keep the game running until the import is (re)run. Returns 1 when the values came from a
 * file, 0 otherwise. */
int ShTables_LoadAnim(const ShAnimTableInfo* info, ShAnimEntry* out);

/* The same while the map overlay `overlay` ("MAP5_S02") is loaded: <name>@<overlay>.json, or the disc copy in that
 * overlay, comes before <name>.json. Each overlay carries its own copy of a table on the disc. */
int ShTables_LoadAnimForOverlay(const ShAnimTableInfo* info, const char* overlay, ShAnimEntry* out);

/* 1 once a loaded table differs from the disc values or came from a mod (a gameplay change). */
int ShTables_GameplayChanged(void);

#ifdef __cplusplus
}
#endif

#endif
