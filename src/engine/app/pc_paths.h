/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_paths.h - where the game's files live (docs/ARCHITECTURE.md)
 *
 * Everything is found from the directory of the executable, never from the
 * current directory:
 *   <exe>/config.cfg   settings
 *   <exe>/assets/      files of the engine itself (fonts, languages, icons, builtin/)
 *   <exe>/gamedata/    data imported from the disc image (gamedata_dir overrides it)
 *   <exe>/maps/        map overlays
 *   <exe>/.savegames/  saves (savegame_dir overrides it)
 *
 * "<exe>" is the game's root. Only a macOS application bundle moves it: the
 * bundle cannot be written to, so the root is a folder of the user's
 * (engine/app/app_bundle.h), and the binaries stay in the bundle (BinDir).
 */
#ifndef PC_PATHS_H
#define PC_PATHS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game's root with a trailing separator: the directory of the executable,
 * or the root set by PcPaths_SetRoot. Falls back to "./" when the platform
 * cannot tell. */
const char* PcPaths_ExeDir(void);

/* The directory the executable and its libraries and tools are in, with a
 * trailing separator. The same as PcPaths_ExeDir except in a macOS bundle. */
const char* PcPaths_BinDir(void);

/* The read-only files the package brings (assets/, upscale-models/, ...): a
 * bundle's Contents/Resources/, otherwise the same as PcPaths_ExeDir. */
const char* PcPaths_ResourceDir(void);

/* Moves the root and the resources (both with a trailing separator); before
 * anything has asked for a path. */
void PcPaths_SetRoot(const char* root, const char* resources);

/* <exe>/<rel> into out. Returns out; out is empty when the path does not fit. */
char* PcPaths_ExeFile(char* out, size_t size, const char* rel);

/* <exe>/assets/<rel> into out. Returns out; out is empty when the path does not fit. */
char* PcPaths_Asset(char* out, size_t size, const char* rel);

#ifdef __cplusplus
}
#endif

#endif /* PC_PATHS_H */
