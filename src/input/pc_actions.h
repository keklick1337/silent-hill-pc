/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_actions.h - C bridge to the semantic input actions (engine/input/
 * input_actions.h). Keyboard and mouse bind to actions;
 * this layer turns the held ones into pad bits through the pad system, so the
 * game logic and replay only ever see the pad word. The controller keeps its
 * per-button pad_* binds.
 *
 * Config: action_<name> = <key>[,<key2>] (classic or global) and
 * action_<name>_altcam = ... (alternate cameras). Game thread only.
 */
#ifndef PC_ACTIONS_H
#define PC_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Config load: reset to the built-in defaults before the file is parsed. */
void PcActions_ResetDefaults(void);
/* One config line. Returns 1 when the key is an action key (consumed, even if
 * the value was rejected with a warning), 0 otherwise. */
int  PcActions_ParseConfigLine(const char* key, const char* value);
/* After the parse. migrateLegacy = 1 (config older than v8): actions the file
 * does not name take the player's customized key_* PS1-button binds, and the
 * result is written back as action_* lines. Then projects the PC-only actions
 * onto their legacy fields (keyReload, keyFlashlight, ...), which their code reads. */
void PcActions_FinishLoad(int migrateLegacy);

/* Bind screens: "action_<name>" = primary slot, "action_<name>_2" = secondary.
 * Same contract as PcConfig_BindField / PcConfig_BindDefault; NULL / "" for a
 * key that is not an action slot. */
char*       PcActions_BindField(const char* key, int scheme, int* outPerScheme);
const char* PcActions_BindDefault(const char* key, int scheme);
/* Writes the action behind an action slot key to config.cfg (both slots in
 * one line) and re-projects the PC-only fields. Returns 0 for a non-action key. */
int         PcActions_Save(const char* key, int scheme);
/* The config line for an action slot key, for batched writes. */
int         PcActions_ConfigLine(const char* key, int scheme, char* outKey, int keyCap, char* outValue, int valueCap);
void        PcActions_Project(void);

/* Which scheme the keyboard drives: 0 classic, 1 alternate cameras. */
void PcActions_SetScheme(int scheme);

/* Actions held by the on-screen controls of a touch screen (bit n = sh::input::Action n): one more device beside
 * the keyboard and the mouse, latched per press the same way (input/touch/touch_controls.h). */
void PcActions_SetTouchActions(unsigned int held);

/* Devices kept out of the pad word: bit 0 the keyboard (the touch layout editor owns Back), bit 1 the mouse (a
 * desktop testing the touch controls, where the mouse is a finger). */
void PcActions_SetSuspended(int devices);

/* Installed as the pad source: the keyboard/mouse pad word, active-low. */
unsigned short PcActions_PadWord(void);
/* PS1 bits currently held by mouse buttons or the wheel (active-high). Menus
 * subtract these so a click is read as a click only. */
unsigned int   PcActions_MouseBits(void);
/* PS1 bits held by keys that count as one press, not as a held button
 * (active-high): text does not fast-forward on them. */
unsigned int   PcActions_TapBits(void);
/* 1 if the pause press the game is taking now came from Back (Escape) in free
 * gameplay rather than from the Pause action; clears the record either way. */
int            PcActions_TakePauseFromBack(void);

/* Console ACTIONS: line i ("run             Left Shift, Right Shift") for the
 * active scheme; 0 past the end. */
int PcActions_Describe(int i, char* out, int cap);

/* Context source (pc_actions_ctx.cpp): writes the 13 controller
 * config masks in s_ControllerConfig order (enter..map) and returns the
 * sh::input::Context value for the current screen. */
int PcActions_CtxQuery(unsigned short outCfg[13]);

#ifdef __cplusplus
}
#endif

#endif /* PC_ACTIONS_H */
