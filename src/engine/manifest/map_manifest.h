/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Vladislav Tislenko (keklick1337) */
/* The game manifest (gamedata/game.json): the game's own facts — title,
 * log prefix, disc executable/containers, map table, start maps — live in
 * data, not code. This C face keeps the engine generic: every accessor
 * returns NULL when the manifest is absent and callers fall back to their
 * built-in defaults. */
#ifndef MAP_MANIFEST_H
#define MAP_MANIFEST_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The manifest lives at <exe>/gamedata/game.json; the library never asks the
 * platform for the exe directory itself (it is linked into tools/tests too),
 * the executable injects it once, before anything logs. */
void MapManifest_SetRoot(const char* exe_dir);

/* Reads gamedata/game.json once (later calls are a single flag test) and
 * returns the number of map entries it provided; 0 = no usable manifest. */
int MapManifest_Load(void);

/* Replaces the manifest with `text` (gamedata/game.json merged with the
 * active mods' patches of it, done by the game, which knows the mods) and
 * returns the number of map entries. Values read before stay as they were
 * read (the journal's log_prefix, the disc names of the VFS). NULL forgets
 * the manifest: the next accessor reads gamedata/game.json again. */
int MapManifest_LoadText(const char* text);

/* Top-level string fields: "title", "exe_name", "log_prefix", "start_map",
 * "new_game_map", "intro_file", "intro_file_extra", "opening_file". NULL when
 * absent. */
const char* MapManifest_String(const char* key);

/* Top-level int fields: "intro_frame_offset". 0 when absent. */
int MapManifest_Int(const char* key);

/* disc{} string fields: "exe_file" (ISO name, e.g. SLUS_007.07), "game_id"
 * (version.cfg id, e.g. SLUS-007.07), "region". NULL when absent. */
const char* MapManifest_DiscString(const char* key);

const char* MapManifest_DiscContainer(int index);

/* Map table entries by array position (the name is the identifier; a legacy
 * numeric "id" field is ignored). NULL keeps the caller's compiled-in
 * fallback. */
const char* MapManifest_Name(int id);
const char* MapManifest_Description(int id);

/* 0 when the manifest has no map table. */
int MapManifest_Count(void);

#ifdef __cplusplus
}
#endif

#endif /* MAP_MANIFEST_H */
