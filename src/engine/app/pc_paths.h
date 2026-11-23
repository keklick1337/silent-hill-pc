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
 */
#ifndef PC_PATHS_H
#define PC_PATHS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Directory of the executable with a trailing separator. Falls back to "./"
 * when the platform cannot tell. */
const char* PcPaths_ExeDir(void);

/* <exe>/<rel> into out. Returns out; out is empty when the path does not fit. */
char* PcPaths_ExeFile(char* out, size_t size, const char* rel);

/* <exe>/assets/<rel> into out. Returns out; out is empty when the path does not fit. */
char* PcPaths_Asset(char* out, size_t size, const char* rel);

#ifdef __cplusplus
}
#endif

#endif /* PC_PATHS_H */
