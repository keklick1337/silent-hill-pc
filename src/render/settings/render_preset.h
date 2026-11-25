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
    RENDER_PRESET_MOBILE             = 7, /* phones and tablets: enhanced at a render scale from the screen, light */
    RENDER_PRESET_CUSTOM             = 8, /* no preset: compiled-in defaults plus the file */
    RENDER_PRESET_COUNT
};

/* A phone starts with the preset made for it; a desktop with enhanced. */
#ifdef __ANDROID__
#define RENDER_PRESET_DEFAULT RENDER_PRESET_MOBILE
#else
#define RENDER_PRESET_DEFAULT RENDER_PRESET_ENHANCED
#endif

/* A phone or tablet tells its screen's height in pixels (its shorter side) before the config is read: from then on
 * every preset above original is its phone variant (render_preset.cpp, RENDER.md 7.2): mobile at half the screen's
 * lines, enhanced and high at all of them with every light and shadow at a low quality, ultra at 648 lines with TAA;
 * no film grain, no flake shadows, trilinear filtering at most. Without a call (a desktop) the presets are as they
 * are, and mobile's scale is 0.75 (a 1080-line screen); 0 goes back to that. */
void  RenderPreset_SetScreenHeight(int pixels);
float RenderPreset_MobileRenderScale(void);

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

/* What the platform cannot run, switched off in cfg whatever the preset or the file says: on Android the AI
 * upscalings, ray tracing and the anisotropic texture filters. Nothing elsewhere. RenderPreset_Apply and
 * RenderSettings_Push call it. */
void RenderPreset_PlatformLimits(s_PcConfig* cfg);

/* Pushes the render-quality fields of g_PcConfig that have no other push site
 * (render_scale, deband, soft_highlights, shadows, shadow_filter) into the renderer and logs
 * the effective preset. Call before sh::platform::start: render_scale sizes the
 * scene target when the window is created. */
void RenderSettings_Push(void);

#ifdef __cplusplus
}
#endif

#endif
