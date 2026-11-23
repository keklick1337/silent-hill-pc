#include <SDL3/SDL.h>
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "engine/app/pc_paths.h"

#include <stdio.h>
#include <string.h>


static char s_ExeDir[1024];

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
