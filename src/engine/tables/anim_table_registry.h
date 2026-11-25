#ifndef SH_ENGINE_TABLES_ANIM_TABLE_REGISTRY_H
#define SH_ENGINE_TABLES_ANIM_TABLE_REGISTRY_H

/* The registry of the animation tables: names, array sizes and disc sources (anim_table_registry.cpp, part of
 * sh_tables); the values come from gamedata/tables/anim/ (the disc import writes them from the map overlays). Nothing
 * is compiled into the game: a missing table file is an error the game reports. */

#include "engine/tables/sh_tables.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Element count of a static array (the registry lists). */
#define ANIM_DEFAULTS_COUNT(arr) (sizeof(arr) / sizeof((arr)[0]))

/* Largest `count` of the tables (CYBIL_ANIM_INFOS). */
#define ANIM_TABLE_MAX_ENTRIES 64

/* The tables, by file name. */
extern const ShAnimTableInfo* const g_AnimTableInfos[];
extern const unsigned int           g_AnimTableInfoCount;

/* Functions a variable-duration entry can name. */
extern const ShAnimFunc   g_AnimTableFuncs[];
extern const unsigned int g_AnimTableFuncCount;

extern const ShAnimTableInfo g_AlessaAnimTable;
extern const ShAnimTableInfo g_BloodsuckerAnimTable;
extern const ShAnimTableInfo g_BloodyIncubatorAnimTable;
extern const ShAnimTableInfo g_BloodyLisaAnimTable;
extern const ShAnimTableInfo g_CatAnimTable;
extern const ShAnimTableInfo g_CherylAnimTable;
extern const ShAnimTableInfo g_CreeperAnimTable;
extern const ShAnimTableInfo g_CybilAnimTable;
extern const ShAnimTableInfo g_DahliaAnimTable;
extern const ShAnimTableInfo g_FlaurosAnimTable;
extern const ShAnimTableInfo g_FloatstingerAnimTable;
extern const ShAnimTableInfo g_GhostChildAlessaAnimTable;
extern const ShAnimTableInfo g_GhostDoctorAnimTable;
extern const ShAnimTableInfo g_GroanerAnimTable;
extern const ShAnimTableInfo g_HangedScratcherAnimTable;
extern const ShAnimTableInfo g_HarryM6S04ExtraAnimTable;
extern const ShAnimTableInfo g_IncubatorAnimTable;
extern const ShAnimTableInfo g_IncubusAnimTable;
extern const ShAnimTableInfo g_KaufmannAnimTable;
extern const ShAnimTableInfo g_LarvalStalkerAnimTable;
extern const ShAnimTableInfo g_LisaAnimTable;
extern const ShAnimTableInfo g_LittleIncubusAnimTable;
extern const ShAnimTableInfo g_LockerDeadBodyAnimTable;
extern const ShAnimTableInfo g_MonsterCybilAnimTable;
extern const ShAnimTableInfo g_ParasiteAnimTable;
extern const ShAnimTableInfo g_PuppetNurseAAnimTable;
extern const ShAnimTableInfo g_PuppetNurseBAnimTable;
extern const ShAnimTableInfo g_RomperAnimTable;
extern const ShAnimTableInfo g_SplitHeadAnimTable;
extern const ShAnimTableInfo g_TwinfeelerAnimTable;
extern const ShAnimTableInfo g_Unkkown23AnimTable;

#ifdef __cplusplus
}
#endif

#endif
