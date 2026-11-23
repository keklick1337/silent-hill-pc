/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_controls_ref.h - one-screen reference of the player's current bindings
 * (keyboard, mouse and gamepad) for every input action, grouped by category.
 * Read-only: rebinding stays in the Controls screen. Layout comes from
 * engine/input/controls_ref.h. Game thread only.
 */
#ifndef PC_CONTROLS_REF_H
#define PC_CONTROLS_REF_H

#ifdef __cplusplus
extern "C" {
#endif

void Pc_ControlsRef_Open(void);
int  Pc_ControlsRef_IsOpen(void);
/* Reads input, draws the screen, closes on cancel / confirm / click. Returns 1
 * while the screen owns the frame, 0 once it is closed (or was not open). */
int  Pc_ControlsRef_Update(void);

#ifdef __cplusplus
}
#endif

#endif
