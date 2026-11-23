/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * pc_quick_actions.h - the PC-only action binds outside the PS1 pad mapping
 * (reload, cycle weapons, quick heal, quick turn, flashlight, rear look and the
 * optional weapon number keys / wheel). This layer reads the keyboard and the
 * physical controller; the decisions are the engine's (engine/items/quick_actions.h).
 */
#ifndef PC_QUICK_ACTIONS_H
#define PC_QUICK_ACTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

/* One frame of the action binds; called from the main loop before the combat machine. */
void Pc_ExtraActionsUpdate(void);
/* Rear Look: held in a TPS/OTS camera during gameplay. */
void Pc_RearLookUpdate(void);

/* Frame-stable rising edge of a physical controller button (SDL gamepad index); < 0 never fires. */
unsigned int PC_RawControllerButtonClicked(int sdlButton);

/* The latched manual reload request; a raw set leaves the latch's countdown. */
int  Pc_ReloadRequestGet(void);
void Pc_ReloadRequestSet(int onOff);
/* 1 = a dry fire on an empty clip starts a reload (auto_reload, or a touchscreen). */
int  Pc_AutoReloadEnabled(void);
/* Console EQUIP: 1 when Harry holds the owned weapon now. */
int  Pc_EquipOwnedWeapon(int invItemId);

/* One-frame quick turn request (consumed by the player's state machine), rear look held,
 * and the number keys / wheel switch (config quick_weapons). */
extern int g_PcQuickTurnRequest;
extern int g_PcRearLookActive;
extern int g_PcQuickWeaponsEnabled;

#ifdef __cplusplus
}
#endif

#endif /* PC_QUICK_ACTIONS_H */
