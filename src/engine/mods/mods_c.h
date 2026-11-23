/* C face of the mod registry (engine/mods/registry.h) for the C-style loaders. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The file of the highest active mod that has `relative` (a gamedata path such as "CHARA/HERO.TIM" or
 * "convert/textures/CHARA/HERO.png"; case-insensitive) in `out`; 0 when no active mod has it. */
int ShMods_FindFile(const char* relative, char* out, size_t out_size);

/* Number of active mods (0 = the game runs as shipped). */
int ShMods_ActiveCount(void);

#ifdef __cplusplus
}
#endif
