// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// The game state the named script API (named_api.cpp) reads and writes outside the engine's own
// classes: the character records, the running save record, the frame globals and the map-state variables of the
// loaded map (map code globals registered under game-logic names).

#pragma once

#include <cstdint>
#include <string_view>

namespace sh::script::named_state {

// --- Map-state variables -----------------------------------------------------------------------------------------

// Element type of a map-state variable; Ref elements are addressable (reference arguments) but not numbers.
enum class VarType : std::uint8_t { S8, U8, S16, U16, S32, U32, Ref };

enum class VarLookup : std::uint8_t {
    Found,
    Unknown,  // no variable of this name on the loaded map
    Error,    // registered, but this element cannot be reached (the reason is in err)
};

// Element `index` of the map-state variable `name` of the loaded map ("cutscene_timer", "ufo_light_pos").
VarLookup map_variable(std::string_view name, long long index, void** address, VarType* type, char* err, int err_cap);

// --- Records the named API passes by reference -------------------------------------------------------------------

// The character record of slot `slot` (0 the player, 1.. the NPC slots); nullptr out of range.
void* character_record(int slot);
// Parts of that record: its position (3 x Q12), its rotation (3 x Q12 turns) and its model.
void* character_position(void* record);
void* character_rotation(void* record);
void* character_model(void* record);
// Bone transform `n` of the player's skeleton / of the NPC bone pool; nullptr out of range.
void* player_bone(int n);
void* npc_bone(int n);
// The second character model buffer; file buffer `n` (the cutscene and scene loads); nullptr when there is none.
void* model_buffer_2();
void* file_buffer(int n);
// The script scratch picture descriptors (img:0..15).
void* scratch_image(int n);
constexpr int kScratchImages = 16;

// --- Character record fields -------------------------------------------------------------------------------------

enum class CharaField : std::uint8_t {
    PosX, PosY, PosZ, RotX, RotY, RotZ,
    CharaId, PaletteIdx, ControlState, StateStep,
    AnimStatus, AnimFlags, AnimTime, AnimKeyframe, AnimAlpha,
    AngleToTarget, FallSpeed, MoveSpeed, HeadingAngle, Flags, AttackReceived, Health, DeathTimer, AuxTimer,
    CollisionState,
    ScratcherFlagsLow,  // the Hanged Scratcher's flag byte (first byte of the type's variables)
};

// Reads (set false) or writes `value` (truncated to the field's width) of a field of a character record.
void chara_field(void* record, CharaField field, bool set, long long& value);

// --- Frame globals and the running save record -------------------------------------------------------------------

enum class GameField : std::uint8_t {
    DeltaTime, DeltaTimeRaw, GravitySpeed, VBlanks, ActiveBuffer, ScreenFadeStatus, BackgroundGamma,
    SaveMapIdx, SaveLocationId, SaveClearGameCount, SaveClearGameEndings, SaveCurrentEndingFlags, SaveNextFearMode,
    SaveGameDifficulty,
    SaveEnemyState,  // the enemy state word of the current map
};

void game_field(GameField field, bool set, long long& value);

// --- Operations on records ----------------------------------------------------------------------------------------

// The animation buffer size of character group `group` (0..3); false for a bad group.
bool set_anim_buffer_size(int group, std::int32_t bytes);
// The active event's point of interest (Q12); false without an active event.
bool event_point(std::int32_t* x, std::int32_t* z);
// The item that started the active event: whether one did, and its number.
bool event_item_used();
int event_used_item();
// The camera looks along the frame `frame` (a bone transform) turned by `rotation` from `position`.
void camera_look_at_frame(void* frame, void* rotation, void* position);
// The staff roll in perspective: started / one tick (true while it runs).
void credits_start();
bool credits_update();
// The player puts the weapon away for a scene / takes it back.
void player_weapon_stash();
void player_weapon_restore();
// map3_s03's plate door: its picture read into the inspection image; the queue index.
int plate_door_image_load();

}  // namespace sh::script::named_state
