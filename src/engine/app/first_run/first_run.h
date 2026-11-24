/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * first_run.h - the import of the disc image before the game starts
 *
 * C facade over the C++20 first-run code (sh_first_run): the executable is
 * C and C++17, the importer is C++20.
 */
#ifndef PC_FIRST_RUN_H
#define PC_FIRST_RUN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PcFirstRunArgs
{
    const char* exe_dir;      /* with a trailing separator */
    const char* config_path;  /* config.cfg: gamedata_dir and the import_* keys */
    const char* gamedata_arg; /* -data on the command line, or NULL */
    const char* image_arg;    /* --image on the command line, or NULL */
    int         reimport;     /* --reimport */
    const char* language;     /* config.cfg's language (a text directory's code), "auto" or NULL: the locale's */
    const char* import_script; /* --import-script: drives the import screen (tests and screenshots), or NULL */
} PcFirstRunArgs;

typedef struct PcFirstRunResult
{
    char gamedata_dir[1024]; /* absolute */
    /* The raw image (.bin or raw .iso) for the readers that still take the
     * disc directly; empty when none is known. */
    char disc_image[1024];
} PcFirstRunResult;

/* Imports the image into gamedata/ when gamedata/version.cfg is missing, or
 * again on --reimport or import_overwrite = 1, with a progress window (or
 * progress on stderr when there is no display). Returns 0 when the game may
 * start, otherwise the exit code: 1 no image or the import failed, 2 cancelled. */
int PcFirstRun_Run(const PcFirstRunArgs* args, PcFirstRunResult* out);

#ifdef __cplusplus
}
#endif

#endif /* PC_FIRST_RUN_H */
