// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Switches and entry points of other modules that the console commands drive and that have no header of their
// own. Each is declared with the linkage of its definition; the owning module is named beside it.

#pragma once

// engine/app/loop/main_loop.cpp: the cheats and developer views of the running game.
extern int g_DebugNoWallCollision;
extern int g_DebugNoTarget;
extern int g_DebugCamEnabled;
extern int g_DebugAnimKfView;
extern int g_DebugAnimKf;
extern int g_DebugAnimKfMax;
extern int g_PcGodMode;
extern int g_PcInfiniteAmmo;
extern int g_PcFastForward;
extern int g_PcFmvClockDiscard;

// engine/app/control_scheme.cpp
extern int g_PcAllowDebugControls;

// engine/inventory/game/item_stage.cpp: how the inventory shows its items.
extern int g_PcInvAspectSquare;
extern int g_PcInvAspectPct;
extern int g_PcInvCarouselYOff;
extern int g_PcInvEquipYOff;
extern int g_PcInvDimStrength;

// engine/runtime/cell_stream_runtime.cpp
extern int g_PcChunkCollisionLocalScope;

// The door fade's step, the loads' pace and the third-person orbit.
extern int g_PcTxnFadeTimestep;
extern int g_PcFastBlockingLoads;
extern int g_TpsCamYaw;

extern "C" {

// engine/app/gameflow/map_load_host.cpp: the loading screen's timing.
int Pc_LoadScreenHoldFrames(void);
void Pc_LoadScreenHoldFramesSet(int frames);
int Pc_LoadScreenPaceVblanks(void);
void Pc_LoadScreenPaceVblanksSet(int vblanks);
int Pc_LoadScreenMinVblanks(void);
void Pc_LoadScreenMinVblanksSet(int vblanks);

// engine/gameflow/game/title_host.cpp: a New Game on a map, run between frames (0: refused now).
int Pc_MapWarp_Request(int map);

// engine/app/devtools/free_camera_host.cpp, engine/camera/camera_entry.cpp
void Pc_FreeCam_Set(int on);
void Pc_CamSnapDump(void);

}  // extern "C"

// ui/screens/options_screen.cpp; weak so the tools link without the Options screen.
extern "C" int Pc_Options_OpenFromConsole(const char* page) __attribute__((weak));
