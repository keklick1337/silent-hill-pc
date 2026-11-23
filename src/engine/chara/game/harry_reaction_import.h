/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Harry's reaction clips on demand. An enemy spawned on a map that does not host
 * it finds none of the rows its attacks make Harry play (the map's reaction table and his map clips are
 * the map's own); this loads them from one of the enemy's maps: the rows, the clips and their keyframes
 * are appended to the current map's tables. Only engine spawns (console, debug menu, scripts) ask, so
 * the map's own play is untouched. C-safe. */
#ifndef SH_ENGINE_CHARA_GAME_HARRY_REACTION_IMPORT_H
#define SH_ENGINE_CHARA_GAME_HARRY_REACTION_IMPORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* Makes the reactions character.json "harry_reactions" of the type names playable on the current map
 * (with gamedata/chara/Harry/reactions.json "hostile" when `hostile`). Returns how many stay missing
 * (0: all playable), -1 when the data cannot be read. */
int Pc_HarryReactions_EnsureFor(int charaId, int hostile);

/* The row of reaction `id` in the current table: its index, first and last keyframe; -1 when none. */
int Pc_HarryReactions_Row(int id, int* first, int* last);

/* The rows Player_OverlayAnimStart/Set walk: the original 40, more when this module extended the table.
 * Also puts an extended table back into a header that was swapped for a copy (a language switch). */
int Pc_HarryReactions_RowScan(void);

/* 1 when row `index` of the current table was loaded by this module. */
int Pc_HarryReactions_RowImported(int index);

/* A map (re)load: its tables are fresh, the loaded clips go. */
void Pc_HarryReactions_MapLoaded(void);

#ifdef __cplusplus
}

namespace sh::maps {
class TableNames;
}
/* The names a map's tables.json data uses: items by name, Harry's reactions by
 * chara/Harry/reactions.json clip name. */
const sh::maps::TableNames& HarryReactions_TableNames();
#endif

#endif
