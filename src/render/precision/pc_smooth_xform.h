/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PC_SMOOTH_XFORM_H
#define PC_SMOOTH_XFORM_H

/* Full-precision model transforms for drawing (config key smooth_models).
 *
 * The game builds every model's view matrix in fixed point: animation
 * interpolates bone translations to whole Q8 units and rotations to Q12, each
 * level of the bone hierarchy truncates again in the GTE, and the root sits at
 * the character position rounded from Q12 to Q8. PGXP removes the rounding of
 * the projected vertices, but not these steps, so a still or slowly moving
 * character still twitches by fractions of a PSX pixel per bone (several
 * pixels at 4K), and its parts shift against each other.
 *
 * This module keeps a double-precision copy of the same values next to the
 * integer ones: the animation records the unrounded interpolation, the drawer
 * recomposes the hierarchy and the camera in double and hands the result to
 * the PGXP projection as the exact twin of the integer view matrix. A frame
 * is the address of the game's coordinate record, read through
 * render/game_view.h. Logic,
 * collision and every integer the game reads are unchanged; a value the game
 * changed after it was recorded fails the integer key check and falls back to
 * the integer one. Inactive unless both smooth_models and PGXP are on. */

#ifdef __cplusplus
extern "C" {
#endif

/* Nonzero when the twins are built this frame (smooth_models and PGXP on). */
int  Pc_Smooth_Active(void);

/* Records the unrounded local transform the animation just wrote into `coord`.
 * `rotQ12` is the 3x3 in Q12 units (row-major) or NULL, `trans` the
 * translation in the coord's integer units or NULL. Call after the integer
 * values are stored: they are the validation key. */
void Pc_Smooth_BoneLocal(void* coord, const double* rotQ12, const double* trans);
/* `dst` got `src`'s integer translation copied; copy the twin with it. */
void Pc_Smooth_BoneCopyTrans(void* dst, const void* src);
/* Root translation from the owner's Q12 world position (coord.t = pos >> 4). */
void Pc_Smooth_RootPosition(void* root, int posXQ12, int posYQ12, int posZQ12);

/* Registers the double view matrix of `coord` as the PGXP twin of `viewMat`,
 * which the caller just computed with Vw_CoordToWorldAndViewMatrices. */
void Pc_Smooth_CoordView(void* coord, void* viewMat);

/* Test hook: the double world transform of `coord` (rot Q12 units, trans int
 * units). Returns 0 if any twin failed validation (integer values used). */
int  Pc_Smooth_WorldOf(const void* coord, double rotQ12[9], double trans[3]);

#ifdef __cplusplus
}
#endif

#endif
