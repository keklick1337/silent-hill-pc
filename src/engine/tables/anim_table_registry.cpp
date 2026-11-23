// SPDX-License-Identifier: GPL-3.0-or-later
// The registry of the animation tables: each table's name, the size of the game's array and its place on
// the disc. No values are compiled in: the tables are read from gamedata/tables/anim/ (written by the import from
// gamedata/raw/) into the engine's clip tables (engine/chara/game/clip_tables.cpp) before any map is
// loaded. Where a table lies in each overlay: the "source" of each docs/FORMATS.md table file.

#include "engine/tables/anim_table_registry.h"

extern "C" {

static const ShAnimSource s_Alessa_sources[] = {
    { "VIN/MAP3_S02.BIN", 0x800D1B6C },
    { "VIN/MAP6_S02.BIN", 0x800D37A8 },
    { "VIN/MAP6_S04.BIN", 0x800EA9D8 },
    { "VIN/MAP7_S03.BIN", 0x800EA920 },
};
static const ShAnimSource s_Bloodsucker_sources[] = {
    { "VIN/MAP3_S03.BIN", 0x800D5ABC },
    { "VIN/MAP7_S02.BIN", 0x800E7988 },
};
static const ShAnimSource s_BloodyIncubator_sources[] = {
    { "VIN/MAP7_S03.BIN", 0x800EAA80 },
};
static const ShAnimSource s_BloodyLisa_sources[] = {
    { "VIN/MAP7_S01.BIN", 0x800DFFD8 },
    { "VIN/MAP7_S02.BIN", 0x800E7948 },
    { "VIN/MAP7_S03.BIN", 0x800EAF60 },
};
static const ShAnimSource s_Cat_sources[] = {
    { "VIN/MAP1_S01.BIN", 0x800DC924 },
};
static const ShAnimSource s_Cheryl_sources[] = {
    { "VIN/MAP0_S00.BIN", 0x800DF174 },
};
static const ShAnimSource s_Creeper_sources[] = {
    { "VIN/MAP1_S02.BIN", 0x800E0D38 },
    { "VIN/MAP1_S03.BIN", 0x800E0F78 },
    { "VIN/MAP3_S01.BIN", 0x800D3CB8 },
    { "VIN/MAP3_S02.BIN", 0x800D16D4 },
    { "VIN/MAP3_S05.BIN", 0x800D9328 },
    { "VIN/MAP5_S00.BIN", 0x800D9CBC },
};
static const ShAnimSource s_Cybil_sources[] = {
    { "VIN/MAP0_S01.BIN", 0x800DD890 },
    { "VIN/MAP4_S01.BIN", 0x800D5150 },
    { "VIN/MAP6_S01.BIN", 0x800D35A0 },
    { "VIN/MAP7_S03.BIN", 0x800EA1E0 },
};
static const ShAnimSource s_Dahlia_sources[] = {
    { "VIN/MAP2_S01.BIN", 0x800CFDEC },
    { "VIN/MAP3_S06.BIN", 0x800D1CDC },
    { "VIN/MAP6_S01.BIN", 0x800D39A0 },
    { "VIN/MAP6_S04.BIN", 0x800EAB38 },
    { "VIN/MAP7_S02.BIN", 0x800E7B68 },
    { "VIN/MAP7_S03.BIN", 0x800EA5E0 },
};
static const ShAnimSource s_Flauros_sources[] = {
    { "VIN/MAP6_S04.BIN", 0x800EAE80 },
};
static const ShAnimSource s_Floatstinger_sources[] = {
    { "VIN/MAP4_S05.BIN", 0x800D7864 },
};
static const ShAnimSource s_GhostChildAlessa_sources[] = {
    { "VIN/MAP7_S01.BIN", 0x800DFC78 },
    { "VIN/MAP7_S02.BIN", 0x800E7EA8 },
};
static const ShAnimSource s_GhostDoctor_sources[] = {
    { "VIN/MAP7_S02.BIN", 0x800E82A8 },
};
static const ShAnimSource s_Groaner_sources[] = {
    { "VIN/MAP2_S00.BIN", 0x800EEB14 },
    { "VIN/MAP2_S02.BIN", 0x800EAE8C },
    { "VIN/MAP4_S02.BIN", 0x800EAE68 },
    { "VIN/MAP5_S01.BIN", 0x800ED704 },
    { "VIN/MAP6_S00.BIN", 0x800ED784 },
};
static const ShAnimSource s_HangedScratcher_sources[] = {
    { "VIN/MAP5_S00.BIN", 0x800D91A0 },
    { "VIN/MAP6_S03.BIN", 0x800DB1C0 },
};
static const ShAnimSource s_HarryM6S04Extra_sources[] = {
    { "VIN/MAP6_S04.BIN", 0x800CC424 },
};
static const ShAnimSource s_Incubator_sources[] = {
    { "VIN/MAP7_S03.BIN", 0x800EAC00 },
};
static const ShAnimSource s_Incubus_sources[] = {
    { "VIN/MAP7_S03.BIN", 0x800EC808 },
};
static const ShAnimSource s_Kaufmann_sources[] = {
    { "VIN/MAP3_S00.BIN", 0x800D1EE0 },
    { "VIN/MAP5_S02.BIN", 0x800D8E40 },
    { "VIN/MAP5_S03.BIN", 0x800D3338 },
    { "VIN/MAP7_S02.BIN", 0x800E7FC8 },
    { "VIN/MAP7_S03.BIN", 0x800EAC80 },
};
static const ShAnimSource s_LarvalStalker_sources[] = {
    { "VIN/MAP1_S00.BIN", 0x800DA6C8 },
    { "VIN/MAP1_S01.BIN", 0x800DA3D8 },
    { "VIN/MAP6_S04.BIN", 0x800E81CC },
};
static const ShAnimSource s_Lisa_sources[] = {
    { "VIN/MAP3_S04.BIN", 0x800D5064 },
    { "VIN/MAP4_S04.BIN", 0x800D30D4 },
    { "VIN/MAP7_S00.BIN", 0x800D2CA8 },
    { "VIN/MAP7_S01.BIN", 0x800DFD98 },
    { "VIN/MAP7_S02.BIN", 0x800E7708 },
};
static const ShAnimSource s_LittleIncubus_sources[] = {
    { "VIN/MAP7_S03.BIN", 0x800EAFA8 },
};
static const ShAnimSource s_LockerDeadBody_sources[] = {
    { "VIN/MAP1_S03.BIN", 0x800E1410 },
};
static const ShAnimSource s_MonsterCybil_sources[] = {
    { "VIN/MAP6_S04.BIN", 0x800EA718 },
};
static const ShAnimSource s_Parasite_sources[] = {
    { "VIN/MAP6_S04.BIN", 0x800EAEC0 },
};

/* The two parallel inline anim_info blocks of the Puppet Nurse record in MAP3_S03 (the PSX symbol
 * table has no names for them). B follows A contiguously on the disc. */
static const ShAnimSource s_PuppetNurseA_sources[] = {
    { "VIN/MAP3_S03.BIN", 0x800D5150 },
};

static const ShAnimSource s_PuppetNurseB_sources[] = {
    { "VIN/MAP3_S03.BIN", 0x800D5430 },
};
static const ShAnimSource s_Romper_sources[] = {
    { "VIN/MAP2_S02.BIN", 0x800EC954 },
    { "VIN/MAP4_S02.BIN", 0x800EC930 },
    { "VIN/MAP5_S01.BIN", 0x800EF1CC },
    { "VIN/MAP6_S00.BIN", 0x800EF24C },
};
static const ShAnimSource s_SplitHead_sources[] = {
    { "VIN/MAP1_S05.BIN", 0x800D5888 },
    { "VIN/MAP1_S06.BIN", 0x800D6E34 },
};
static const ShAnimSource s_Twinfeeler_sources[] = {
    { "VIN/MAP4_S03.BIN", 0x800DAE90 },
};
static const ShAnimSource s_Unkkown23_sources[] = {
    { "VIN/MAP7_S03.BIN", 0x800ECE50 },
};

const ShAnimTableInfo g_AlessaAnimTable = {
    "alessa", "ALESSA_ANIM_INFOS", 22, 22,
    s_Alessa_sources, 4, 22};
const ShAnimTableInfo g_BloodsuckerAnimTable = {
    "bloodsucker", "BLOODSUCKER_ANIM_INFOS", 30, 30,
    s_Bloodsucker_sources, 2, 30};
const ShAnimTableInfo g_BloodyIncubatorAnimTable = {
    "bloody_incubator", "BLOODY_INCUBATOR_ANIM_INFOS", 24, 24,
    s_BloodyIncubator_sources, 1, 24};
const ShAnimTableInfo g_BloodyLisaAnimTable = {
    "bloody_lisa", "BLOODY_LISA_ANIM_INFOS", 4, 4,
    s_BloodyLisa_sources, 3, 4};
const ShAnimTableInfo g_CatAnimTable = {
    "cat", "CAT_ANIM_INFOS", 10, 10,
    s_Cat_sources, 1, 10};
const ShAnimTableInfo g_CherylAnimTable = {
    "cheryl", "CHERYL_ANIM_INFOS", 8, 8,
    s_Cheryl_sources, 1, 8};
const ShAnimTableInfo g_CreeperAnimTable = {
    "creeper", "CREEPER_ANIM_INFOS", 36, 36,
    s_Creeper_sources, 6, 36};
const ShAnimTableInfo g_CybilAnimTable = {
    "cybil", "CYBIL_ANIM_INFOS", 64, 64,
    s_Cybil_sources, 4, 64};
const ShAnimTableInfo g_DahliaAnimTable = {
    "dahlia", "DAHLIA_ANIM_INFOS", 52, 52,
    s_Dahlia_sources, 6, 52};
const ShAnimTableInfo g_FlaurosAnimTable = {
    "flauros", "FLAUROS_ANIM_INFOS", 4, 4,
    s_Flauros_sources, 1, 4};
const ShAnimTableInfo g_FloatstingerAnimTable = {
    "floatstinger", "FLOATSTINGER_ANIM_INFOS", 28, 26,
    s_Floatstinger_sources, 1, 26};
const ShAnimTableInfo g_GhostChildAlessaAnimTable = {
    "ghost_child_alessa", "GHOST_CHILD_ALESSA_ANIM_INFOS", 18, 18,
    s_GhostChildAlessa_sources, 2, 18};
const ShAnimTableInfo g_GhostDoctorAnimTable = {
    "ghost_doctor", "GHOST_DOCTOR_ANIM_INFOS", 4, 4,
    s_GhostDoctor_sources, 1, 4};
const ShAnimTableInfo g_GroanerAnimTable = {
    "groaner", "GROANER_ANIM_INFOS", 48, 48,
    s_Groaner_sources, 5, 48};
const ShAnimTableInfo g_HangedScratcherAnimTable = {
    "hanged_scratcher", "HANGED_SCRATCHER_ANIM_INFOS", 54, 54,
    s_HangedScratcher_sources, 2, 54};
const ShAnimTableInfo g_HarryM6S04ExtraAnimTable = {
    "harry_m6s04_extra", "HARRY_M6S04_EXTRA_ANIM_INFOS", 8, 8,
    s_HarryM6S04Extra_sources, 1, 256 /* the entries are copied into Harry's map table: no link limit here. */};
const ShAnimTableInfo g_IncubatorAnimTable = {
    "incubator", "INCUBATOR_ANIM_INFOS", 8, 8,
    s_Incubator_sources, 1, 8};
const ShAnimTableInfo g_IncubusAnimTable = {
    "incubus", "INCUBUS_ANIM_INFOS", 12, 12,
    s_Incubus_sources, 1, 12};
const ShAnimTableInfo g_KaufmannAnimTable = {
    "kaufmann", "KAUFMANN_ANIM_INFOS", 46, 46,
    s_Kaufmann_sources, 5, 46};
const ShAnimTableInfo g_LarvalStalkerAnimTable = {
    "larval_stalker", "LARVAL_STALKER_ANIM_INFOS", 38, 38,
    s_LarvalStalker_sources, 3, 38};
const ShAnimTableInfo g_LisaAnimTable = {
    "lisa", "LISA_ANIM_INFOS", 36, 36,
    s_Lisa_sources, 5, 36};
const ShAnimTableInfo g_LittleIncubusAnimTable = {
    "little_incubus", "LITTLE_INCUBUS_ANIM_INFOS", 6, 6,
    s_LittleIncubus_sources, 1, 6};
const ShAnimTableInfo g_LockerDeadBodyAnimTable = {
    "locker_dead_body", "LOCKER_DEAD_BODY_ANIM_INFOS", 8, 8,
    s_LockerDeadBody_sources, 1, 8};
const ShAnimTableInfo g_MonsterCybilAnimTable = {
    "monster_cybil", "MONSTER_CYBIL_ANIM_INFOS", 44, 44,
    s_MonsterCybil_sources, 1, 44};
const ShAnimTableInfo g_ParasiteAnimTable = {
    "parasite", "PARASITE_ANIM_INFOS", 6, 6,
    s_Parasite_sources, 1, 6};
const ShAnimTableInfo g_PuppetNurseAAnimTable = {
    "puppet_nurse_a", "PUPPET_NURSE_A_ANIM_INFOS", 46, 46,
    s_PuppetNurseA_sources, 1, 46};
const ShAnimTableInfo g_PuppetNurseBAnimTable = {
    "puppet_nurse_b", "PUPPET_NURSE_B_ANIM_INFOS", 46, 46,
    s_PuppetNurseB_sources, 1, 46};
const ShAnimTableInfo g_RomperAnimTable = {
    "romper", "ROMPER_ANIM_INFOS", 44, 44,
    s_Romper_sources, 4, 44};
const ShAnimTableInfo g_SplitHeadAnimTable = {
    "split_head", "SPLIT_HEAD_ANIM_INFOS", 30, 30,
    s_SplitHead_sources, 2, 30};
const ShAnimTableInfo g_TwinfeelerAnimTable = {
    "twinfeeler", "TWINFEELER_ANIM_INFOS", 48, 48,
    s_Twinfeeler_sources, 1, 48};
const ShAnimTableInfo g_Unkkown23AnimTable = {
    "unkkown_23", "UNKKOWN_23_ANIM_INFOS", 12, 12,
    s_Unkkown23_sources, 1, 12};

const ShAnimTableInfo* const g_AnimTableInfos[] = {
    &g_AlessaAnimTable,
    &g_BloodsuckerAnimTable,
    &g_BloodyIncubatorAnimTable,
    &g_BloodyLisaAnimTable,
    &g_CatAnimTable,
    &g_CherylAnimTable,
    &g_CreeperAnimTable,
    &g_CybilAnimTable,
    &g_DahliaAnimTable,
    &g_FlaurosAnimTable,
    &g_FloatstingerAnimTable,
    &g_GhostChildAlessaAnimTable,
    &g_GhostDoctorAnimTable,
    &g_GroanerAnimTable,
    &g_HangedScratcherAnimTable,
    &g_HarryM6S04ExtraAnimTable,
    &g_IncubatorAnimTable,
    &g_IncubusAnimTable,
    &g_KaufmannAnimTable,
    &g_LarvalStalkerAnimTable,
    &g_LisaAnimTable,
    &g_LittleIncubusAnimTable,
    &g_LockerDeadBodyAnimTable,
    &g_MonsterCybilAnimTable,
    &g_ParasiteAnimTable,
    &g_PuppetNurseAAnimTable,
    &g_PuppetNurseBAnimTable,
    &g_RomperAnimTable,
    &g_SplitHeadAnimTable,
    &g_TwinfeelerAnimTable,
    &g_Unkkown23AnimTable,
};

const unsigned int g_AnimTableInfoCount = ANIM_DEFAULTS_COUNT(g_AnimTableInfos);

const ShAnimFunc g_AnimTableFuncs[] = {
    { "MonsterCybil_GetAnimInfo", (int32_t)0x800D8898u },
};

const unsigned int g_AnimTableFuncCount = ANIM_DEFAULTS_COUNT(g_AnimTableFuncs);
}  // extern "C"
