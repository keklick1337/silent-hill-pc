/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The fixed sound slots of a character ("sounds" in the character data) as a C enum, consumed
 * through Pc_CharaSfx. Other call sites name the slot as a string through Pc_CharaSfxNamed
 * (engine/chara/runtime/chara_data.h): any snake_case name is a slot, no enum entry needed.
 * Standalone and C-safe. */
#pragma once

enum Pc_CharaSfxSlot {
    PC_CHARA_SFX_FOOTSTEP,        /* "footstep" */
    PC_CHARA_SFX_FOOTSTEP_GRASS,  /* "footstep_grass" */
    PC_CHARA_SFX_FOOTSTEP_METAL,  /* "footstep_metal" */
    PC_CHARA_SFX_DEATH,           /* "death" */
    PC_CHARA_SFX_ATTACK,          /* "attack" */
    PC_CHARA_SFX_DAMAGE,          /* "damage" */
};
