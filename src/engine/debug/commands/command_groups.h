// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The groups of the game's console commands, one file each (register_game_commands in command_set.cpp adds
// them all). Each function registers its commands with their arguments, help and completion.

#pragma once

#include "engine/console/registry.h"

namespace sh::debug::commands {

void register_system_commands(sh::console::Registry& registry);     // quit, version, menus, binds, window, log, fps
void register_capture_commands(sh::console::Registry& registry);    // shot, shotwin, shotseq, vramdump, dump
void register_script_commands(sh::console::Registry& registry);     // pad, stall, rngseed
void register_world_commands(sh::console::Registry& registry);      // map, pos, tp, event, fmv, saves, difficulty
void register_inspect_commands(sh::console::Registry& registry);    // charas, textures, actions, langcheck
void register_flag_commands(sh::console::Registry& registry);       // flags, setflag, getflags, clearflags, setending
void register_item_commands(sh::console::Registry& registry);       // give, equip, givemap, saturate
void register_spawn_commands(sh::console::Registry& registry);      // spawn, spawnpreview, turntable
void register_selection_commands(sh::console::Registry& registry);  // select, info, health, kill, freeze, goto, ...
void register_cheat_commands(sh::console::Registry& registry);      // god, noclip, notarget, infammo, views
void register_camera_commands(sh::console::Registry& registry);     // fov, third person, view scale and shift
void register_display_commands(sh::console::Registry& registry);    // aspect, brightness, text sizes, inventory view
void register_render_commands(sh::console::Registry& registry);     // draw distance, fog, culling, PGXP, gpubench
void register_light_commands(sh::console::Registry& registry);      // flashlight, shadows, post-processing
void register_loading_commands(sh::console::Registry& registry);    // loads, transitions, framebuffer feedback
void register_perf_commands(sh::console::Registry& registry);       // perfrec, perfbench

}  // namespace sh::debug::commands
