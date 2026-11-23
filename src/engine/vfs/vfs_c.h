/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ENGINE_VFS_VFS_C_H
#define ENGINE_VFS_VFS_C_H

/* C facade of the VFS for the C code of the engine. Implemented in
 * vfs_c.cpp (target sh_vfs_c). */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the VFS once: mod folders (<exe>/mods, <exe>/data/mods and the
 * config key mods_dir, relative to the exe) over gamedata/raw. gamedata_dir is
 * the folder the first run resolved (PcPaths, config key gamedata_dir). When
 * it is a finished import the disc host (engine/platform/disc.h) is installed, so
 * every read by LBA comes from gamedata/ and the mods and the disc image is
 * never opened; returns 1 then. Returns 0 when there is no usable gamedata/:
 * the caller falls back to reading the disc image.
 * Environment SH_VFS_VERIFY_IMAGE=<.cue or .bin>: check mode, every sector
 * read through the VFS is compared with the same sector of that image;
 * SH_VFS_VERIFY_LOG=<file> takes the [VFS/VERIFY] lines (stderr otherwise). */
int ShVfs_InitGame(const char* gamedata_dir, const char* exe_dir, const char* config_path);

int ShVfs_GameDataActive(void);

/* The gamedata folder ShVfs_InitGame was given. */
const char* ShVfs_GameDataDir(void);

/* Whole file, malloc'd (caller frees); NULL when missing. */
unsigned char* ShVfs_ReadFile(const char* path, size_t* out_size);
/* Host file of a mod that replaces the path (the highest mod layer that has
 * it): fills out and returns 1. 0 when no mod has the path; gamedata/raw is
 * never returned, its files are read by sector like the disc. */
int ShVfs_ModFilePath(const char* path, char* out, size_t out_size);
/* The game path of the file that starts at disc sector `lba` ("SND/B.KDT", "XA/05_02152"): fills out and returns 1,
 * or 0 when no file starts there. From the game's file table, so it works without raw/. */
int ShVfs_PathAtLba(int lba, char* out, size_t out_size);
/* Host file of a converted asset, `relative` to convert/ ("models/CHARA/HERO.glb",
 * "textures/CHARA/HERO.png"): the highest mod's <mod>/convert/<relative>, else
 * gamedata/convert/<relative>. Fills out and returns 1; 0 when none has it. */
int ShVfs_FindConverted(const char* relative, char* out, size_t out_size);
/* The same over the mods only (gamedata/convert/ is never returned). */
int ShVfs_FindModConverted(const char* relative, char* out, size_t out_size);
/* Host file of a gamedata file, `relative` to the gamedata root
 * ("maps/areas/ER.glb"): the highest mod's <mod>/<relative>, else
 * gamedata/<relative>. Fills out and returns 1; 0 when none has it. */
int ShVfs_FindDataFile(const char* relative, char* out, size_t out_size);
/* The same over the mods only (gamedata/<relative> is never returned). */
int ShVfs_FindModDataFile(const char* relative, char* out, size_t out_size);
/* A JSON gamedata file with every active mod's patch merged over gamedata/<relative> in load
 * order (engine/mods/data_merge.h). malloc'd, NUL-terminated text (free() it) and its size in
 * *out_size; NULL when neither gamedata/ nor a mod has the file, or the base does not parse
 * (logged). Without mod copies the bytes are the file's own. */
char* ShVfs_ReadDataJson(const char* relative, size_t* out_size);

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_VFS_VFS_C_H */
