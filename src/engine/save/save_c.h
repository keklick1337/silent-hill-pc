/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Vladislav Tislenko (keklick1337) */
#ifndef ENGINE_SAVE_SAVE_C_H
#define ENGINE_SAVE_SAVE_C_H

/* C facade of the save store (engine/save) for the game. Saves are numbered (save_NNNN.sav,
 * number >= 1), quick saves 0-based (0..9). Results: 1 done, 0 nothing there / not done, -1 damaged or failed, -2 a
 * save made by a newer version of the game, which is never read into the game (the reason is in the journal,
 * category save). Implemented in save_c.cpp (target sh_save_c); C++ code reaches the
 * store itself through save_service.h.
 *
 * A save is the engine's SaveGame (save_game.h, names only). The game hands over and takes back its running progress
 * record and settings (sh::game::SaveRecord, sh::game::Settings); the facade converts both ways with the names the
 * game installed. */

#include <stdint.h>

#include "engine/game_state/progress_record.h"
#include "engine/game_state/settings_record.h"

extern "C" {

typedef struct ShSaveSummary
{
    int32_t  sequence;      /* 0 = empty for the game */
    uint32_t play_time;     /* 1/4096 s */
    uint16_t save_count;
    uint8_t  location;
    uint8_t  next_fear;
    uint8_t  extra_hours;
    uint8_t  special_items;
} ShSaveSummary;

/* Name <-> number for the things a save names. A name function returns
 * NULL for a number without a name, a number function -1 for a name it does not know. Any of them may be NULL. */
typedef struct ShSaveNameTable
{
    const char* (*item_name)(int number);
    int (*item_number)(const char* name);
    const char* (*flag_name)(int number);
    int (*flag_number)(const char* name);
    const char* (*map_name)(int number);
    int (*map_number)(const char* name);
    const char* (*paper_map_name)(int number);
    int (*paper_map_number)(const char* name);
    /* The flag a name meant in a save written before that name moved to another flag; NULL for other names. */
    const char* (*flag_name_in_old_save)(const char* name);
} ShSaveNameTable;

/* Directory: `setting` (config savegame_dir) when set, absolute or relative to `exe_dir`; else
 * <exe_dir>/.savegames. Created on the first write. */
void        ShSave_Open(const char* exe_dir, const char* setting);
const char* ShSave_Root(void);
/* Copied; install before the migration and the first read. */
void        ShSave_SetNames(const ShSaveNameTable* names);

/* Once per store: imports the old <dir>/N.MCD card files, the images in <root>/import/ and the saves of the old
 * card1/card2 layout (memory_card.h); returns the number of saves added. */
int ShSave_MigrateLegacy(const char* const* dirs, int count);

/* Numbers of the saves this game can load (readable, not made by a newer version of the game), newest first, at
 * most `capacity` of them; returns how many there are. */
int     ShSave_ListNewest(int32_t* numbers, int capacity);
/* The same; `listed` (may be NULL) gets the number of saves the load list has to offer: those and the saves of a
 * newer game, which it shows and refuses. */
int     ShSave_ListLoadable(int32_t* numbers, int capacity, int* listed);
/* Highest sequence of the saves, 0 when there are none. */
int32_t ShSave_NewestSequence(void);

/* `state` gets the save's progress record. On anything but 1 `state` is untouched. */
int ShSave_Read(int number, ShSaveSummary* summary, sh::game::SaveRecord* state);
/* The options saved with save `number` into `options`: 1, or 0 when the save has none (`options` untouched), -1 when
 * the save is damaged. */
int ShSave_ReadOptions(int number, sh::game::Settings* options);
/* number <= 0: a new save. `options` may be NULL. Returns the number written, or -1. */
int ShSave_Write(int number, const ShSaveSummary* summary, const sh::game::SaveRecord* state,
                 const sh::game::Settings* options);
int ShSave_Delete(int number);

int ShSave_QuickRead(int index, ShSaveSummary* summary, sh::game::SaveRecord* state);
/* The optional RMNS chunk (JSON, engine/entities/remains.h): the text the next write stores ("" or NULL: none),
 * and the text of the last save read into a state ("": it had none). */
void ShSave_SetRemains(const char* text);
const char* ShSave_LoadedRemains(void);
int ShSave_QuickWrite(int index, const ShSaveSummary* summary, const sh::game::SaveRecord* state,
                      const sh::game::Settings* options);
/* The same for a save file at any path (a state dump's state.sav), outside the store's folders. */
int ShSave_FileRead(const char* path, ShSaveSummary* summary, sh::game::SaveRecord* state);
int ShSave_FileWrite(const char* path, const ShSaveSummary* summary, const sh::game::SaveRecord* state,
                     const sh::game::Settings* options);

}

#endif /* ENGINE_SAVE_SAVE_C_H */
