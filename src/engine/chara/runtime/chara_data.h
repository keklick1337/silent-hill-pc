/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Vladislav Tislenko (keklick1337) */
/* The running game's character data, gamedata/chara/: the library, the action hooks
 * (chara/<Name>/actions.lua) and the footstep tables, loaded on first use from <exe>/gamedata and <exe>/mods.
 *
 * C-safe part: the accessors the game code calls with the original's value as the fallback. Every one
 * answers that value when the library, the character or the key is absent; the shipped data carries the same
 * values, so nothing changes until a mod or the user edits it. */
#ifndef SH_ENGINE_CHARA_RUNTIME_CHARA_DATA_H
#define SH_ENGINE_CHARA_RUNTIME_CHARA_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Sound id of a named slot ("footstep", "death", ...): the character's data, then its action hook. */
int Pc_CharaSfxNamed(int charaId, const char* slot, int originalSfx);

/* A sound table of the original played by index names each index's slot:
 *   PC_CHARA_SFX_TABLE(Chara_Twinfeeler, TWINFEELER_SOUNDS) = { { "stand_up", 1558 }, ... };
 * `sfx` is the id the original table holds. */
typedef struct Pc_CharaSfxSlotEntry
{
    const char* slot;
    int         sfx;
} Pc_CharaSfxSlotEntry;
#define PC_CHARA_SFX_TABLE(charaId, name) static const Pc_CharaSfxSlotEntry name[]

/* A registry sound by name ("footstep_concrete") -> its id; -1 when unknown. */
int Pc_SoundIdFind(const char* name);

/* Sound id of an enum slot (Pc_CharaSfxSlot, engine/chara/chara_sfx_slot.h). */
int Pc_CharaSfx(int charaId, int slot, int originalSfx);

/* A numeric parameter as Q12 ((s32)((float)value * 4096.0f)), as an integer, and a named clip's index. */
int Pc_CharaParamQ12(int charaId, const char* name, int originalQ12);
int Pc_CharaParamInt(int charaId, const char* name, int original);
int Pc_CharaAnim(int charaId, const char* name, int originalAnim);

/* A footstep: floor material -> surface -> the character's table (Harry's when it has none) -> the map's place
 * rules -> on_footstep hook. Fills the sound and the surface's pitch jitters (the caller draws them). */
typedef struct Pc_FootstepQuery {
    int charaId;
    int material;
    int mapIdx;
    int roomIdx;
    int playerX, playerY, playerZ; /* Q12 */
} Pc_FootstepQuery;
typedef struct Pc_FootstepChoice {
    int sfx;
    int walkBase, walkRange;
    int runBase, runRange;
} Pc_FootstepChoice;
void Pc_FootstepChoose(const Pc_FootstepQuery* query, Pc_FootstepChoice* choice);
/* The character's footsteps table and hook only (no place rules). */
int Pc_CharaFootstepSfx(int charaId, int material, int originalSfx);

/* Sounds with their own file ("file" entries of gamedata/sounds): played instead of a bank tone with the
 * caller's volume (0..0x3FFF per side) and pitch offset in cents. */
int Pc_SoundIsFile(int sfxId);
int Pc_SoundFilePlay(int sfxId, int left, int right, int pitchCents);
void Pc_SoundFileStop(int sfxId);
void Pc_SoundFileVolume(int sfxId, int left, int right);

/* "Chara_Groaner" / "Groaner" / "4" -> 4; -1 when unknown. */
int Pc_CharaIdFind(const char* name);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus) && !defined(SH_C_ABI_REGION)
namespace sh::chara {
class CharaLibrary;
class CharaActions;
class Footsteps;
struct CharacterDef;

// nullptr when the data could not be loaded.
const CharaLibrary* game_library();
// The data of a character type (its traits); nullptr for an unknown number or without data.
const CharacterDef* type_def(int chara_id);
// The type is the player's character (character.json "is_player").
bool is_player_type(int chara_id);
CharaActions* game_actions();
}  // namespace sh::chara

// Older names of game_library() / game_actions().
const sh::chara::CharaLibrary* Pc_CharaData_Library();
sh::chara::CharaActions* Pc_CharaData_Actions();
#endif

#endif
