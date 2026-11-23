// SPDX-License-Identifier: GPL-3.0-or-later
//
// The world object steps of the running map: only a map with
// gamedata/maps/<map>/world_objects.json draws through the world object runtime (world_object_runtime.h); on any
// other map the steps that draw do nothing, so a stale table of the previous map is never drawn.
#pragma once

namespace sh::world {

// The objects and the script's chunk, then its post_init() (set-up that needs the objects in place).
void map_objects_init();
// The script's pre_draw() (0 skips this frame's draws: map6_s04 runs its carousel instead), the draws, its
// post_draw(), then the killed-enemy write-back of a map with spawn variants.
void map_objects_update();
// The room's spawn pass: the script's npc_spawn().
void map_objects_npc_spawn();
// After the cameras move: the script's post_camera() (drawing that needs this frame's camera).
void map_objects_post_camera();

}  // namespace sh::world
