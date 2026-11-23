/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef RENDER_SETTINGS_RENDER_PRESET_H
#define RENDER_SETTINGS_RENDER_PRESET_H

/* Graphics presets (config key graphics_preset, docs/RENDER.md,
 * presets section).
 *
 * A preset only sets the DEFAULTS of the render-quality keys: it is applied to
 * the config before config.cfg is parsed, so any of those keys present in the
 * file still wins. None of them touches game logic, timing or what the
 * game decides to draw. */

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* pc_config.h has no C++ guard of its own. */
#include "engine/config/pc_config.h"

enum
{
    RENDER_PRESET_ORIGINAL           = 0, /* PSX look: native 320x224, dither, point sampling */
    RENDER_PRESET_ENHANCED           = 1, /* window resolution, filtering, MSAA, per-pixel flashlight */
    RENDER_PRESET_HIGH               = 2, /* enhanced + flashlight shadows, aniso, grain, HDR, bloom, TAA, AO, lamps */
    RENDER_PRESET_ULTRA              = 3, /* every feature but ray tracing at its best (supersampling, 4096 shadows) */
    RENDER_PRESET_ULTRA_UPSCALED     = 4, /* ultra plus the experimental model and texture upscaling */
    RENDER_PRESET_ULTRA_RTX          = 5, /* ultra plus every ray-traced effect */
    RENDER_PRESET_ULTRA_RTX_UPSCALED = 6, /* ultra_rtx plus the experimental model and texture upscaling */
    RENDER_PRESET_CUSTOM             = 7, /* no preset: compiled-in defaults plus the file */
    RENDER_PRESET_COUNT
};

#define RENDER_PRESET_DEFAULT RENDER_PRESET_ENHANCED

/* -1 for an unknown name. Case-insensitive. */
int         RenderPreset_FromName(const char* name);
const char* RenderPreset_Name(int preset);

/* The last graphics_preset value in an open config file, or the default when
 * the key is absent or unknown. Rewinds the file before returning. *named, when
 * not null, becomes 1 if the file sets the key and 0 otherwise. */
int  RenderPreset_Scan(FILE* f, int* named);

/* 1 when every render-quality key of cfg has the preset's value. */
int  RenderPreset_Matches(int preset, const s_PcConfig* cfg);

/* Writes the preset's values into cfg. Custom (or an invalid id) leaves cfg
 * as it is. */
void RenderPreset_Apply(int preset, s_PcConfig* cfg);

/* Pushes the render-quality fields of g_PcConfig that have no other push site
 * (render_scale, deband, soft_highlights, shadows, shadow_filter) into the renderer and logs
 * the effective preset. Call before sh::platform::start: render_scale sizes the
 * scene target when the window is created. */
void RenderSettings_Push(void);

#ifdef __cplusplus
}
#endif

#endif
