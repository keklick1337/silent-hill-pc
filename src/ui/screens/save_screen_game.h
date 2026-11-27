/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Vladislav Tislenko (keklick1337) */
#pragma once

/* The save screen (ui/screens/save_screen.cpp) and the game under it. The screen
 * keeps the list, the cursor and the prompts; the save and the load are the engine's (engine/save/game_saves.h);
 * the game around them (fades, resets, panels, the next state) is ui/screens/save_screen_game.cpp. */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum PcSaveScreenKind
{
    PcSaveScreen_Save     = 0, /* at a notebook (or F6 with save_anywhere) */
    PcSaveScreen_Load     = 1, /* LOAD on the title, F8 */
    PcSaveScreen_AutoLoad = 2, /* CONTINUE on the title, auto load at boot: the newest save, no list */
    PcSaveScreen_QuickLoad = 3 /* the quick load key with quick_save_instant: the quick save, no list */
} PcSaveScreenKind;

/* Called by the game state functions: one tick of the screen; `entering` on the first tick of the state. */
void Pc_SaveScreen_Update(int kind, int entering);

/* The first tick: fade in, the game's per-screen resets. */
void PcSaveGame_Enter(int kind);
/* The save-screen backdrop (SAVELOAD.TIM) and, for the save screen, the music the original keeps going. */
void PcSaveGame_BackgroundDraw(int kind);
/* A translucent panel with the bevelled gold-and-silver rim of the original info box, in 320x224 text space. */
void PcSaveGame_PanelDraw(int x, int y, int w, int h);
/* The rim alone, `glow` 0..128 brighter (the selected row). */
void PcSaveGame_FrameDraw(int x, int y, int w, int h, int glow);
/* Select on the pad (no menu binds it: Square is one of the cancel buttons), the Delete key or the touch Delete
 * (its button, or a long press on a row), this tick. */
int PcSaveGame_DeletePressed(void);
/* On every tick a save that can be deleted is selected: the touch controls show their Delete. */
void PcSaveGame_AskTouchDelete(void);
/* The device the delete hint names (sh::screens::SaveHintDevice: keys, touch, pad). */
int PcSaveGame_HintDevice(void);
/* The picture of the pad button that deletes: its width in the 320x224 text space (0: none to draw), and the
 * picture at (x, y) of that space. */
int PcSaveGame_DeleteGlyphWidth(void);
void PcSaveGame_DeleteGlyphDraw(int x, int y);
/* That button's name in UTF-8, "" when none is bound (the hint without a picture). */
const char* PcSaveGame_DeleteButtonName(void);
/* The game came here from the boot logo (auto load), not from the title or gameplay. */
int PcSaveGame_FromBoot(void);

/* After a load: starts the game on the loaded map behind a fade; PcSaveGame_StartDone is 1 once handed over. */
void PcSaveGame_Start(void);
int  PcSaveGame_StartDone(void);
/* Back out: fade out (`boot`: the auto load at boot found nothing, the title comes next); PcSaveGame_LeaveDone
 * switches to where the screen was opened from (1 when done). */
void PcSaveGame_Leave(int kind, int boot);
int  PcSaveGame_LeaveDone(int kind);
/* The auto load found nothing to load: the load list takes over (the game state becomes the load screen). */
void PcSaveGame_ToLoadScreen(void);

/* From gameplay: the load screen opens to load the quick save of `slot` (0-based) at once, with no list. */
void PcSaveGame_OpenQuickLoad(int slot);
/* The quick slot the opening load screen loads (then forgotten), -1 when it is the list. */
int PcSaveGame_TakeQuickLoadSlot(void);
/* The same for a save file at any path (a state dump's state.sav): the load screen loads it at once.
 * The screen takes the path ("" when there is none). */
void PcSaveGame_OpenFileLoad(const char* path);
const char* PcSaveGame_TakeLoadFile(void);

#ifdef __cplusplus
}
#endif

