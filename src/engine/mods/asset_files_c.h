/* C face of asset_names.h over the mod registry, for the C-style readers of disc files. Every call returns
 * "nothing" at once, without touching the file system, when no mod is active. */
#pragma once

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the active mods' file for a mod-relative path ("CHARA/HERO.TIM", "1ST/FONT16.png"); NULL when none has it. */
FILE* ShModAsset_Open(const char* relative, const char* mode);

/* ShMods_FindFile with the no-mod fast path. */
int ShModAsset_Find(const char* relative, char* out, size_t out_size);

/* A mod replaces the picture of the TIM at `disc_path` (whole or some palette row). */
int ShModAsset_TimHasImage(const char* disc_path);

/* The replacement is per palette row (asset_names.h: some row >= 1 is shipped). */
int ShModAsset_TimPerRow(const char* disc_path);

/* The picture for palette row `row` (row 0 = the whole-picture name) in `out`; *is_dds tells a BC7 .dds. */
int ShModAsset_TimRow(const char* disc_path, int row, char* out, size_t out_size, int* is_dds);

/* The whole-picture replacement of a non-per-row set (.dds before .png) in `out`. */
int ShModAsset_TimWhole(const char* disc_path, char* out, size_t out_size, int* is_dds);

/* Calls `visit` once per file name directly inside folder `relative_dir` ("CHARA") of the active mods (folder
 * names case-insensitive, a name shipped by several mods once, sorted); returns the number of names. */
int ShModAsset_ListDir(const char* relative_dir, void (*visit)(const char* name, void* ctx), void* ctx);

/* Calls `visit` with the host file of every active mod that has `relative`, the winning (last loaded) mod first;
 * returns the count. */
int ShModAsset_FindEach(const char* relative, void (*visit)(const char* host, void* ctx), void* ctx);

/* Calls `visit` with the host folder `relative_dir` of every active mod that has it, the last loaded mod first;
 * returns the count (`visit` may be NULL to count only). */
int ShModAsset_DirEach(const char* relative_dir, void (*visit)(const char* host_dir, void* ctx), void* ctx);

#ifdef __cplusplus
}
#endif
