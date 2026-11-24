#include <SDL3/SDL.h>
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "engine/app/pc_paths.h"

#include <stdio.h>
#include <string.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdlib.h>
#endif

static char s_ExeDir[1024];
static char s_BinDir[1024];
static char s_ResourceDir[1024];

void PcPaths_SetRoot(const char* root, const char* resources) {
    snprintf(s_ExeDir, sizeof(s_ExeDir), "%s", root ? root : "");
    snprintf(s_ResourceDir, sizeof(s_ResourceDir), "%s", resources ? resources : "");
}

const char* PcPaths_BinDir(void) {
    if (s_BinDir[0] == '\0') {
#ifdef __APPLE__
        /* In a bundle SDL_GetBasePath answers Contents/Resources/: ask for the executable itself. */
        char path[1024];
        uint32_t size = sizeof(path);
        char real[PATH_MAX];
        if (_NSGetExecutablePath(path, &size) == 0 && realpath(path, real) != NULL) {
            char* slash = strrchr(real, '/');
            if (slash != NULL) {
                slash[1] = '\0';
                snprintf(s_BinDir, sizeof(s_BinDir), "%s", real);
            }
        }
#endif
        if (s_BinDir[0] == '\0') {
            const char* base = SDL_GetBasePath();
            snprintf(s_BinDir, sizeof(s_BinDir), "%s", base != NULL && strlen(base) < sizeof(s_BinDir) ? base : "./");
        }
    }
    return s_BinDir;
}

const char* PcPaths_ResourceDir(void) { return s_ResourceDir[0] != '\0' ? s_ResourceDir : PcPaths_ExeDir(); }

const char* PcPaths_ExeDir(void)
{
    if (s_ExeDir[0] == '\0')
    {
        /* SDL_GetBasePath works before SDL_Init. SDL3 owns the string. */
        const char* base = SDL_GetBasePath();

        if (base != NULL && strlen(base) < sizeof(s_ExeDir))
            snprintf(s_ExeDir, sizeof(s_ExeDir), "%s", base);
        else
            snprintf(s_ExeDir, sizeof(s_ExeDir), "./");
    }
    return s_ExeDir;
}

char* PcPaths_ExeFile(char* out, size_t size, const char* rel)
{
    int n = snprintf(out, size, "%s%s", PcPaths_ExeDir(), rel);

    if (n < 0 || (size_t)n >= size)
        out[0] = '\0';
    return out;
}

char* PcPaths_Asset(char* out, size_t size, const char* rel)
{
    int n = snprintf(out, size, "%sassets/%s", PcPaths_ExeDir(), rel);

    if (n < 0 || (size_t)n >= size)
        out[0] = '\0';
    return out;
}
