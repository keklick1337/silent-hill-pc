/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_DEBUG_MENU_H
#define PC_DEBUG_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* The host of the engine's data-driven menus (sh::menu, src/engine/menu): the F10 "DEBUG OPTIONS" menu
 * (menu id "debug") and the PC options ("options") drawn in the pause menu's look. It lives in the
 * pause state in gameplay (the pause machinery freezes and captures the world) and over the title
 * screen on the title. */

/* 1 while a menu owns the input; read by every site that freezes gameplay while the console is up. */
extern int g_PcDebugMenuActive;

/* Once at start: installs sh::menu::host() and lets the debug and options natives register. */
void Pc_DebugMenu_Init(void);

/* F10: opens the "debug" menu, or closes the open one. */
void Pc_DebugMenu_Request(void);
/* Opens a menu as the player-facing screen. 1 when it was opened or will open on the next pause tick,
 * 0 when there is no such menu or the game is not in a state that can host one. */
int  Pc_DebugMenu_OpenById(const char* id);
void Pc_DebugMenu_Close(void);
int  Pc_DebugMenu_IsOpen(void);

/* One tick while open: input, then the draw (the primitives go into this frame). */
void Pc_DebugMenu_Update(void);
void Pc_DebugMenu_Draw(void);

/* Pause side. Gameplay's F10 enters the pause state with the menu pending; the pause menu takes it on
 * its first tick (returns 1 and opens it). */
int  Pc_DebugMenu_Pending(void);
int  Pc_DebugMenu_TakePending(void);
void Pc_DebugMenu_ClearPending(void);
/* 1 when the menu was opened over a running game (F10): closing it resumes the game instead of going
 * back to the pause menu. */
int  Pc_DebugMenu_ResumeOnClose(void);

#ifdef __cplusplus
}
#endif

#endif
