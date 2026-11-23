// SPDX-License-Identifier: GPL-3.0-or-later
// The natives gamedata/menus/options.lua binds its rows to (sh::menu::registry()):
// setting.<config key> for every kept setting, the grouped rows (setting.graphics_preset,
// setting.shadow_quality, setting.flashlight_style, setting.lighting_style), setting.language,
// setting.debug_options, and the actions options.reset_defaults, options.edit_keybinds and
// options.controls_reference. Call once at init, before the menus are defined.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Pc_OptionsBinds_Register(void);

/* A render-quality key of Options (the grouped keys: preset members, ray tracing, upscaling, render_scale): its
 * value in effect, and a change of the running game only, config.cfg untouched (perfbench). 0 for another key. */
int Pc_Options_GraphicsKeyValue(const char* key, float* value);
int Pc_Options_ApplyGraphicsKeyLive(const char* key, float value);

#ifdef __cplusplus
}
#endif
