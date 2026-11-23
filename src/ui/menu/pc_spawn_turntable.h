/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_SPAWN_TURNTABLE_H
#define PC_SPAWN_TURNTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

/* A slowly rotating preview of the character highlighted in the Spawn browsing dropdown, drawn in a
 * panel on the right of the DEBUG OPTIONS menu, before anything is spawned.
 * The model is the character's glb (character.json "model") in its bind pose, drawn by the scene renderer
 * (render/scene/preview.h) into a panel on the right, under the menu's own prims. */

/* The browsing cursor is on `name` (a character folder name, "Groaner"); NULL or "" when it left the
 * list. Cheap; safe to call every move. */
void Pc_SpawnTurntable_Show(const char* name);
void Pc_SpawnTurntable_Hide(void);
/* Names the picked character, shown while the Spawn page is up and no list is being browsed; it returns
 * NULL or "" with the Preview row off. Asked once per Pc_SpawnTurntable_Draw. */
void Pc_SpawnTurntable_SetPicker(const char* (*picked)(void));
/* Whether the menu on screen is the Spawn page (set by the debug menu each frame and on close). */
void Pc_SpawnTurntable_SetPage(int onSpawnPage);

/* 1 when a model can be drawn at all. */
int Pc_SpawnTurntable_Available(void);

/* Once per menu frame, after the menu: advances the turn and draws the panel when available. */
void Pc_SpawnTurntable_Draw(void);

#ifdef __cplusplus
}
#endif

#endif
